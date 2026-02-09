#include "SDL3/SDL_init.h"
#include "SDL3/SDL_video.h"
#include "arena.h"
#include "cglm/types.h"
#include "mesh.h"
#include "print.h"
#include "shader.h"
#include "types.h"
#include "vulkan/vulkan_core.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_vulkan.h>
#include <stb_sprintf.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define VOLK_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <volk.h>
#include <vulkan/vk_enum_string_helper.h>

#define VK_CHECK(result)                                                       \
  {                                                                            \
    if (result != VK_SUCCESS) {                                                \
      print("Vulkan call at %d returned an error: %s\n", __LINE__,             \
            string_VkResult(result));                                          \
    }                                                                          \
  }

#define SDL_CHECK(result)                                                      \
  {                                                                            \
    if (result == 0) {                                                         \
      print("SDL call returned an error: %s\n", SDL_GetError());               \
    }                                                                          \
  }

#define FRAMES_IN_FLIGHT 2

// TODO: abstract the stages (LATER!)
// TODO: make dynamic array impl
// TODO: 1. manual vertex data, cube
// TODO: 2. load gltf / some type of vertex data

typedef struct {

  Mem_Arena  perm_arena;
  VkInstance instance;

  VkPhysicalDevice         GPU;
  VkQueueFamilyProperties* queueFamilies;
  u32                      queueFamilyIndex;

  VkDevice device; // GPU Driver

  VmaAllocator allocator;

  VkSurfaceKHR surface;
  VkFormat     imageFormat;

  VkSwapchainKHR swapchain;
  u32            swapchainImageCount;
  VkImage*       swapchainImages;
  VkImageView*   swapchainImageViews;

  VkImage       depthImage;
  VkImageView   depthImageView;
  VkFormat      depthFormat;
  VmaAllocation depthImageAllocation;

  Vertex* cubeVertices;
  u32     vertexCount;
  u16*    cubeIndices;
  u32     indexCount;

  VkBuffer      vertexBuffer;
  VmaAllocation vertexBufferAllocation;

  ShaderDataBuffer shaderDataBuffers[FRAMES_IN_FLIGHT];
  VkShaderModule   shaderModule; // TODO: load compiled shaders into this
                                 // it is NULL now, will cause validation errors

  VkFence      fences[FRAMES_IN_FLIGHT];
  VkSemaphore  presentSemaphores[FRAMES_IN_FLIGHT];
  VkSemaphore* renderSemaphores;

  VkCommandPool   commandPool;
  VkCommandBuffer commandBuffers[FRAMES_IN_FLIGHT];

  VkPipeline       pipeline;
  VkPipelineLayout pipelineLayout;

  SDL_Window* sdl_window;

} VulkanContext;

int main(void) {

  VulkanContext ctx = {0};
  ctx.perm_arena    = arena_create(GiB(1));

  SDL_CHECK(SDL_Init(SDL_INIT_VIDEO));
  SDL_CHECK(SDL_Vulkan_LoadLibrary(NULL));

  if (volkInitialize() != VK_SUCCESS) {
    return 1;
  }

  VkApplicationInfo appInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                               .pApplicationName = "leafc",
                               .apiVersion       = VK_API_VERSION_1_3};

  /////////////////////////////////
  // fetch platform specific extensions
  /////////////////////////////////

  u32                sdlExtensionCount = 0;
  const char* const* sdlExtensions =
      SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);

  // WARN: This is bad, fix dynamic arrays
  u32          totalExtensionCount = sdlExtensionCount + 1;
  const char** extensions =
      ARENA_PUSH_ARRAY(&ctx.perm_arena, const char*, totalExtensionCount);
  for (s32 i = 0; i < sdlExtensionCount; ++i) {
    extensions[i] = sdlExtensions[i];
  }
  extensions[sdlExtensionCount] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  // WARN:

  const char* validationLayers[] = {"VK_LAYER_KHRONOS_validation"};

  // TODO: verify that VK_LAYER_KHRONOS_validation exists
  u32 layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, NULL);
  VkLayerProperties* availableLayers =
      ARENA_PUSH_ARRAY(&ctx.perm_arena, VkLayerProperties, layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);

  bool layerFound = false;
  for (s32 i = 0; i < layerCount; ++i) {
    print("%s", availableLayers[i].layerName);
    if (strcmp(validationLayers[0], availableLayers[i].layerName) == 0) {
      layerFound = true;
      break;
    }
  }
  if (!layerFound) {
    print("could not find VK_LAYER_KHRONOS_validation\n");
    return 1;
  }

  VkDebugUtilsMessengerCreateInfoEXT debugCI = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugCallback};

  VkInstanceCreateInfo instanceCI = {
      .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pApplicationInfo        = &appInfo,
      .enabledExtensionCount   = totalExtensionCount,
      .ppEnabledExtensionNames = extensions,
      .enabledLayerCount       = 1,
      .ppEnabledLayerNames     = validationLayers,
      .pNext                   = &debugCI,
  };

  VK_CHECK(vkCreateInstance(&instanceCI, NULL, &ctx.instance));
  volkLoadInstance(ctx.instance);

  /////////////////////////////////
  // Device selection
  /////////////////////////////////
  u32 deviceCount = 0;
  VK_CHECK(vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, NULL));
  VkPhysicalDevice* availableGPUs =
      ARENA_PUSH_ARRAY(&ctx.perm_arena, VkPhysicalDevice, deviceCount);
  VK_CHECK(
      vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, availableGPUs));

  ctx.GPU = availableGPUs[0]; // OUR GPU!!!

  VkPhysicalDeviceProperties2 deviceProperties = {0};
  deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  vkGetPhysicalDeviceProperties2(ctx.GPU, &deviceProperties);
  print("selected device: %s\n", deviceProperties.properties.deviceName);

  /////////////////////////////////
  // Queues
  /////////////////////////////////
  u32 queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.GPU, &queueFamilyCount, NULL);
  VkQueueFamilyProperties* queueFamilies = ARENA_PUSH_ARRAY(
      &ctx.perm_arena, VkQueueFamilyProperties, queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.GPU, &queueFamilyCount,
                                           queueFamilies);
  // find a queue with graphics support
  for (s32 i = 0; i < queueFamilyCount; ++i) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      ctx.queueFamilyIndex = i;
      break;
    }
  }

  // check if queue supports presentation
  SDL_CHECK(SDL_Vulkan_GetPresentationSupport(ctx.instance, ctx.GPU,
                                              ctx.queueFamilyIndex));

  const f32               queueFamilyPriorities = 1.0f;
  VkDeviceQueueCreateInfo queueCI               = {
                    .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = ctx.queueFamilyIndex,
                    .queueCount       = 1,
                    .pQueuePriorities = &queueFamilyPriorities,
  };

  /////////////////////////////////
  // Feature check!
  /////////////////////////////////

  // explanation of some of the features enabled, from www.howtovulkan.com
  // * Dynamic rendering - Greatly simplifies render pass setup, one of the most
  // criticized Vulkan areas
  // * Buffer device address - Lets us access buffers via
  // pointers instead of going through descriptors
  // * Descriptor indexing - Simplifies descriptor management, often referred to
  // as "bindless"
  // * Synchronization2 - Improves synchronization handling, one of
  // the hardest areas of Vulkan
  VkPhysicalDeviceFeatures enabledVk10Features = {0};
  enabledVk10Features.samplerAnisotropy = VK_TRUE; // anisotropic filtering

  VkPhysicalDeviceVulkan12Features enabledVk12Features = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .descriptorIndexing                        = true,
      .shaderSampledImageArrayNonUniformIndexing = true,
      .descriptorBindingVariableDescriptorCount  = true,
      .runtimeDescriptorArray                    = true,
      .bufferDeviceAddress                       = true};

  VkPhysicalDeviceVulkan13Features enabledVk13Features = {
      .sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .synchronization2 = true,
      .dynamicRendering = true,
      .pNext            = &enabledVk12Features};

  const char* const deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkDeviceCreateInfo deviceCI = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                 .pNext = &enabledVk13Features,
                                 .queueCreateInfoCount    = 1,
                                 .pQueueCreateInfos       = &queueCI,
                                 .enabledExtensionCount   = 1,
                                 .ppEnabledExtensionNames = deviceExtensions,
                                 .pEnabledFeatures = &enabledVk10Features};

  VK_CHECK(vkCreateDevice(ctx.GPU, &deviceCI, NULL, &ctx.device));

  VmaAllocatorCreateInfo allocatorCI = {
      .flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice   = ctx.GPU,
      .device           = ctx.device,
      .pVulkanFunctions = NULL,
      .instance         = ctx.instance};

  VK_CHECK(vmaCreateAllocator(&allocatorCI, &ctx.allocator));

  ///////////////////////////////////
  // SDL !!!
  ///////////////////////////////////

  ctx.sdl_window = SDL_CreateWindow("leafc", 1280u, 720u,
                                    SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!ctx.sdl_window) {
    print("could not create SDL window: %s\n", SDL_GetError());
  }

  // request a surface from SDL
  SDL_CHECK(SDL_Vulkan_CreateSurface(ctx.sdl_window, ctx.instance, NULL,
                                     &ctx.surface));

  VkSurfaceCapabilitiesKHR surfaceCapabilities = {0};
  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.GPU, ctx.surface,
                                                     &surfaceCapabilities));

  if (surfaceCapabilities.currentExtent.width == UINT32_MAX ||
      surfaceCapabilities.currentExtent.height == UINT32_MAX) {
    s32 w, h;
    surfaceCapabilities.currentExtent.width =
        SDL_GetWindowSizeInPixels(ctx.sdl_window, &w, &h);
    surfaceCapabilities.currentExtent.width  = w;
    surfaceCapabilities.currentExtent.height = h;
  }

  ///////////////////////////////////
  // SWAPCHAIN
  ///////////////////////////////////

  ctx.imageFormat                      = VK_FORMAT_B8G8R8A8_SRGB;
  VkSwapchainCreateInfoKHR swapchainCI = {
      .sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface         = ctx.surface,
      .minImageCount   = surfaceCapabilities.minImageCount,
      .imageFormat     = ctx.imageFormat,
      .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
      .imageExtent =
          (VkExtent2D){.width  = surfaceCapabilities.currentExtent.width,
                       .height = surfaceCapabilities.currentExtent.height},
      .imageArrayLayers = 1,
      .imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform     = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
      .compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode      = VK_PRESENT_MODE_FIFO_KHR  // Vsync
  };

  VK_CHECK(
      vkCreateSwapchainKHR(ctx.device, &swapchainCI, NULL, &ctx.swapchain));

  ctx.swapchainImageCount = 0;
  VK_CHECK(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain,
                                   &ctx.swapchainImageCount, NULL));
  ctx.swapchainImages =
      ARENA_PUSH_ARRAY(&ctx.perm_arena, VkImage, ctx.swapchainImageCount);
  VK_CHECK(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain,
                                   &ctx.swapchainImageCount,
                                   ctx.swapchainImages));
  ctx.swapchainImageViews =
      ARENA_PUSH_ARRAY(&ctx.perm_arena, VkImageView, ctx.swapchainImageCount);

  ///////////////////////////////////
  // DEPTH IMAGES
  ///////////////////////////////////

  const VkFormat depthFormatList[] = {VK_FORMAT_D32_SFLOAT_S8_UINT,
                                      VK_FORMAT_D24_UNORM_S8_UINT};

  for (s32 i = 0; i < sizeof(depthFormatList) / sizeof(VkFormat); ++i) {
    VkFormatProperties2 formatProperties = {0};
    formatProperties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    vkGetPhysicalDeviceFormatProperties2(ctx.GPU, depthFormatList[i],
                                         &formatProperties);
    if (formatProperties.formatProperties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      ctx.depthFormat = depthFormatList[i];
      break;
    }
  }
  s32 w, h;
  SDL_GetWindowSize(ctx.sdl_window, &w, &h);

  VkImageCreateInfo depthImageCI = {
      .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType     = VK_IMAGE_TYPE_2D,
      .format        = ctx.depthFormat,
      .extent        = {.width = w, .height = h, .depth = 1},
      .mipLevels     = 1,
      .arrayLayers   = 1,
      .samples       = VK_SAMPLE_COUNT_1_BIT,
      .tiling        = VK_IMAGE_TILING_OPTIMAL,
      .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
  };

  VmaAllocationCreateInfo allocCI = {
      .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO};
  VK_CHECK(vmaCreateImage(ctx.allocator, &depthImageCI, &allocCI,
                          &ctx.depthImage, &ctx.depthImageAllocation, NULL));

  VkImageViewCreateInfo depthImageViewCI = {
      .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image            = ctx.depthImage,
      .viewType         = VK_IMAGE_VIEW_TYPE_2D,
      .format           = ctx.depthFormat,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                           .levelCount = 1,
                           .layerCount = 1}
  };

  VK_CHECK(vkCreateImageView(ctx.device, &depthImageViewCI, NULL,
                             &ctx.depthImageView));

  ///////////////////////////////////
  // VERTEX DATA to GPU
  ///////////////////////////////////

  ctx.vertexCount = 24; // 4 * 6       (six faces, four vertices each)
  ctx.indexCount  = 36; // 6 * (2 * 3) (six faces of two triangles each)

  ctx.cubeVertices = ARENA_PUSH_ARRAY(&ctx.perm_arena, Vertex, ctx.vertexCount);
  ctx.cubeIndices  = ARENA_PUSH_ARRAY(&ctx.perm_arena, u16, ctx.indexCount);

  generate_cube(ctx.cubeVertices, ctx.cubeIndices);

  VkBufferCreateInfo bufferCI = {
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size  = sizeof(Vertex) * ctx.vertexCount + sizeof(u16) * ctx.indexCount,
      .usage =
          VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
  };

  // flags here make sure we allocate memory on the GPU
  // this avoids using a staging buffer to upload to the GPU
  VmaAllocationCreateInfo bufferAllocCI = {
      .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO,
  };

  VK_CHECK(vmaCreateBuffer(ctx.allocator, &bufferCI, &bufferAllocCI,
                           &ctx.vertexBuffer, &ctx.vertexBufferAllocation,
                           NULL));

  // copy data into VRAM
  void* temp_bufptr = NULL;
  VK_CHECK(
      vmaMapMemory(ctx.allocator, ctx.vertexBufferAllocation, &temp_bufptr));
  memcpy(temp_bufptr, ctx.cubeVertices, sizeof(Vertex) * ctx.vertexCount);
  memcpy((u8*)temp_bufptr + (sizeof(Vertex) * ctx.vertexCount), ctx.cubeIndices,
         sizeof(u16) * ctx.indexCount);
  vmaUnmapMemory(ctx.allocator, ctx.vertexBufferAllocation);

  // creates a buffer for each frame and
  for (s32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    VkBufferCreateInfo uniformBufferCI = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size  = sizeof(ShaderData),
        .usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT};

    VmaAllocationCreateInfo uniformBufferAllocCI = {
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                 VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };

    VK_CHECK(vmaCreateBuffer(ctx.allocator, &uniformBufferCI,
                             &uniformBufferAllocCI,
                             &ctx.shaderDataBuffers[i].buffer,
                             &ctx.shaderDataBuffers[i].allocation, NULL));
    VK_CHECK(vmaMapMemory(ctx.allocator, ctx.shaderDataBuffers[i].allocation,
                          &ctx.shaderDataBuffers[i].mapped));

    VkBufferDeviceAddressInfo uniformBufferDeviceAdressInfo = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = ctx.shaderDataBuffers[i].buffer,
    };

    ctx.shaderDataBuffers[i].deviceAddress =
        vkGetBufferDeviceAddress(ctx.device, &uniformBufferDeviceAdressInfo);
  }

  ///////////////////////////////////
  // SYNCHRONIZATION
  ///////////////////////////////////

  VkSemaphoreCreateInfo semaphoreCI = {
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fenceCI = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                               .flags = VK_FENCE_CREATE_SIGNALED_BIT};
  for (s32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    VK_CHECK(vkCreateFence(ctx.device, &fenceCI, NULL, &ctx.fences[i]));
    VK_CHECK(vkCreateSemaphore(ctx.device, &semaphoreCI, NULL,
                               &ctx.presentSemaphores[i]));
  }
  ctx.renderSemaphores =
      ARENA_PUSH_ARRAY(&ctx.perm_arena, VkSemaphore, ctx.swapchainImageCount);
  for (s32 i = 0; i < ctx.swapchainImageCount; ++i) {
    VK_CHECK(vkCreateSemaphore(ctx.device, &semaphoreCI, NULL,
                               &ctx.renderSemaphores[i]));
  }
  ///////////////////////////////////
  // COMMAND BUFFERS
  ///////////////////////////////////

  VkCommandPoolCreateInfo commandPoolCI = {
      .sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      .queueFamilyIndex = ctx.queueFamilyIndex};
  VK_CHECK(
      vkCreateCommandPool(ctx.device, &commandPoolCI, NULL, &ctx.commandPool));

  VkCommandBufferAllocateInfo commandBufferAllocCI = {
      .sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool        = ctx.commandPool,
      .commandBufferCount = FRAMES_IN_FLIGHT};

  VK_CHECK(vkAllocateCommandBuffers(ctx.device, &commandBufferAllocCI,
                                    ctx.commandBuffers));

  ///////////////////////////////////
  // TODO: TEXTURES
  ///////////////////////////////////

  // KTX ?
  // maybe stb_image

  ///////////////////////////////////
  //  SHADERS
  ///////////////////////////////////
  // TODO: dynamic shader compilation,
  // CLI for now

  ///////////////////////////////////
  //  GRAPHICS PIPELINE
  ///////////////////////////////////

  VkPushConstantRange pushConstantRange = {
      .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
      .size       = sizeof(VkDeviceAddress),
  };
  VkPipelineLayoutCreateInfo pipelineLayoutCI = {
      .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount         = 0,
      .pSetLayouts            = NULL, // TODO: texture descriptor goes here
      .pushConstantRangeCount = 1,
      .pPushConstantRanges    = &pushConstantRange};
  VK_CHECK(vkCreatePipelineLayout(ctx.device, &pipelineLayoutCI, NULL,
                                  &ctx.pipelineLayout));

  VkVertexInputBindingDescription vertexBinding = {
      .binding   = 0,
      .stride    = sizeof(Vertex),
      .inputRate = VK_VERTEX_INPUT_RATE_VERTEX};

  VkVertexInputAttributeDescription vertexAttributes[] = {
      {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT},
      {.location = 1,
       .binding  = 0,
       .format   = VK_FORMAT_R32G32B32A32_SFLOAT,
       .offset   = offsetof(Vertex, normal)},

      {.location = 2,
       .binding  = 0,
       .format   = VK_FORMAT_R32G32_SFLOAT,
       .offset   = offsetof(Vertex, uv)},
  };

  VkPipelineVertexInputStateCreateInfo vertexInputState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
      .vertexBindingDescriptionCount = 1,
      .pVertexBindingDescriptions    = &vertexBinding,
      .vertexAttributeDescriptionCount =
          sizeof(vertexAttributes) / sizeof(VkVertexInputAttributeDescription),
      .pVertexAttributeDescriptions = vertexAttributes,
  };

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {
      .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
  };

  VkPipelineShaderStageCreateInfo shaderStages[] = {
      {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage  = VK_SHADER_STAGE_VERTEX_BIT,
       .module = ctx.shaderModule,
       .pName  = "main"},
      {.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = ctx.shaderModule,
       .pName  = "main"},
  };

  VkPipelineViewportStateCreateInfo viewportState = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount  = 1};

  VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                    VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicState = {
      .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = sizeof(dynamicStates) / sizeof(VkDynamicState),
      .pDynamicStates    = dynamicStates,
  };

  VkPipelineDepthStencilStateCreateInfo depthStencilState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable  = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL};

  VkPipelineRenderingCreateInfo renderingState = {
      .sType                = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &ctx.imageFormat,
      .depthAttachmentFormat   = ctx.depthFormat};

  VkPipelineColorBlendAttachmentState blendAttachment = {
      .colorWriteMask = 0xF,
  };

  VkPipelineColorBlendStateCreateInfo colorBlendState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments    = &blendAttachment};

  VkPipelineRasterizationStateCreateInfo rasterizationState = {
      .sType     = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multisampleState = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

  VkGraphicsPipelineCreateInfo pipelineCI = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext               = &renderingState,
      .stageCount          = 2,
      .pStages             = shaderStages,
      .pVertexInputState   = &vertexInputState,
      .pInputAssemblyState = &inputAssemblyState,
      .pViewportState      = &viewportState,
      .pRasterizationState = &rasterizationState,
      .pMultisampleState   = &multisampleState,
      .pDepthStencilState  = &depthStencilState,
      .pColorBlendState    = &colorBlendState,
      .pDynamicState       = &dynamicState,
      .layout              = ctx.pipelineLayout};
  VK_CHECK(vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineCI,
                                     NULL, &ctx.pipeline));

  // TODO: LOOP
  u64  lastTime = SDL_GetTicksNS();
  bool quit     = false;
  while (!quit) {
    // wait on fence
    // acquire next image
    // update shader data
    // record command buffer
    // submit command buffer
    // present image
    // poll events
  }

  return 0;
}
