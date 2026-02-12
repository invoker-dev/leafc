#include "SDL3/SDL_init.h"
#include "SDL3/SDL_video.h"
#include "arena.h"
#include "cglm/cglm.h"
#include "mesh.h"
#include "print.h"
#include "shader.h"
#include "types.h"
#include "vulkan/vulkan_core.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_vulkan.h>
#include <stb_sprintf.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

#define FRAMES_IN_FLIGHT 3

// TODO: abstract the stages (LATER!)
// TODO: make dynamic array impl
// TODO: 1. manual vertex data, cub
// TODO: 2. load gltf / some type of vertex data

typedef struct {

  Mem_Arena  perm_arena;
  VkInstance instance;

  VkPhysicalDevice         GPU;
  VkQueueFamilyProperties* queueFamilies;
  u32                      queueFamilyIndex;
  VkQueue                  queue;

  VkDevice device; // GPU Driver

  VmaAllocator allocator;

  VkSurfaceKHR             surface;
  VkSurfaceCapabilitiesKHR surfaceCapabilities;
  VkFormat                 imageFormat;

  VkSwapchainKHR swapchain;
  u32            swapchainImageCount;
  VkImage*       swapchainImages;
  VkImageView*   swapchainImageViews;
  u32            imageIndex;
  bool           updateSwapchain;

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

  ShaderData       shaderData;
  ShaderDataBuffer shaderDataBuffers[FRAMES_IN_FLIGHT];
  VkShaderModule   shaderModule;

  VkFence      fences[FRAMES_IN_FLIGHT];
  VkSemaphore  presentSemaphores[FRAMES_IN_FLIGHT];
  VkSemaphore* renderSemaphores;

  VkCommandPool   commandPool;
  VkCommandBuffer commandBuffers[FRAMES_IN_FLIGHT];

  VkPipeline       pipeline;
  VkPipelineLayout pipelineLayout;

  u32 frameIndex;

  SDL_Window* sdl_window;
  SDL_Event   sdl_event;
  bool        running;

  vec3 camPos;
  vec3 cubePos;

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
  for (u32 i = 0; i < sdlExtensionCount; ++i) {
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
  for (u32 i = 0; i < layerCount; ++i) {
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
  for (u32 i = 0; i < queueFamilyCount; ++i) {
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
  vkGetDeviceQueue(ctx.device, ctx.queueFamilyIndex, 0, &ctx.queue);

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

  VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.GPU, ctx.surface,
                                                     &ctx.surfaceCapabilities));

  if (ctx.surfaceCapabilities.currentExtent.width == UINT32_MAX ||
      ctx.surfaceCapabilities.currentExtent.height == UINT32_MAX) {
    s32 w, h;
    ctx.surfaceCapabilities.currentExtent.width =
        SDL_GetWindowSizeInPixels(ctx.sdl_window, &w, &h);
    ctx.surfaceCapabilities.currentExtent.width  = w;
    ctx.surfaceCapabilities.currentExtent.height = h;
  }

  ///////////////////////////////////
  // SWAPCHAIN
  ///////////////////////////////////

  ctx.imageFormat                      = VK_FORMAT_B8G8R8A8_SRGB;
  VkSwapchainCreateInfoKHR swapchainCI = {
      .sType           = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface         = ctx.surface,
      .minImageCount   = ctx.surfaceCapabilities.minImageCount,
      .imageFormat     = ctx.imageFormat,
      .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
      .imageExtent =
          (VkExtent2D){.width  = ctx.surfaceCapabilities.currentExtent.width,
                       .height = ctx.surfaceCapabilities.currentExtent.height},
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

  for (u32 i = 0; i < ctx.swapchainImageCount; ++i) {
    VkImageViewCreateInfo swapchainImageViewCI = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image            = ctx.swapchainImages[i],
        .viewType         = VK_IMAGE_VIEW_TYPE_2D,
        .format           = ctx.imageFormat,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1},

    };
    VK_CHECK(vkCreateImageView(ctx.device, &swapchainImageViewCI, NULL, &ctx.swapchainImageViews[i]));
  }

  ///////////////////////////////////
  // DEPTH IMAGES
  ///////////////////////////////////

  const VkFormat depthFormatList[] = {VK_FORMAT_D32_SFLOAT_S8_UINT,
                                      VK_FORMAT_D24_UNORM_S8_UINT};

  ctx.depthFormat = VK_FORMAT_UNDEFINED;
  for (u32 i = 0; i < sizeof(depthFormatList) / sizeof(VkFormat); ++i) {
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

  assert(ctx.depthFormat != VK_FORMAT_UNDEFINED);

  // WARN: fix this
  s32 w, h;
  SDL_GetWindowSizeInPixels(ctx.sdl_window, &w, &h);

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

  // copy vertex data into VRAM
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
  for (u32 i = 0; i < ctx.swapchainImageCount; ++i) {
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

  size_t compiledShaderSize;
  void*  compiledShader = SDL_LoadFile("shader.spv", &compiledShaderSize);

  VkShaderModuleCreateInfo shaderModuleCI = {
      .sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = compiledShaderSize,
      .pCode    = (u32*)compiledShader,
  };

  VK_CHECK(vkCreateShaderModule(ctx.device, &shaderModuleCI, NULL,
                                &ctx.shaderModule));

  ctx.shaderData = (ShaderData){
      .projection = {0},
      .view       = {0},
      .model      = {0},
      .lightPos   = {0.0f, -10.0f, 10.0f, 0.0f},
      .selected   = 1,
  };

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

  // memory barriers for swapchain coloring

  // TODO: LOOP
  u64 lastTime   = SDL_GetTicksNS();
  ctx.frameIndex = 0;
  ctx.running    = true;
  while (ctx.running) {
    // wait on fence
    VK_CHECK(vkWaitForFences(ctx.device, 1, &ctx.fences[ctx.frameIndex], true,
                             UINT64_MAX));
    VK_CHECK(vkResetFences(ctx.device, 1, &ctx.fences[ctx.frameIndex]));

    // get window size
    s32 w, h;
    SDL_GetWindowSizeInPixels(ctx.sdl_window, &w, &h);

    // acquire next image
    VkResult swapchainResult = vkAcquireNextImageKHR(
        ctx.device, ctx.swapchain, UINT64_MAX,
        ctx.presentSemaphores[ctx.frameIndex], VK_NULL_HANDLE, &ctx.imageIndex);

    if (swapchainResult == VK_ERROR_OUT_OF_DATE_KHR) {
      ctx.updateSwapchain = true;
    } else {
      VK_CHECK(swapchainResult);
    }

    // update shader data
    glm_perspective(glm_rad(45.0f), (float)w / h, 0.1f, 32.0f,
                    ctx.shaderData.projection);
    glm_translate(ctx.shaderData.view, ctx.camPos);

    mat4 identity;
    glm_mat4_identity(identity);
    glm_translate(identity, ctx.cubePos);

    // make data available to GPU
    memcpy(ctx.shaderDataBuffers[ctx.frameIndex].mapped, &ctx.shaderData,
           sizeof(ctx.shaderData));

    // record command buffer
    VkCommandBuffer cmd = ctx.commandBuffers[ctx.frameIndex];
    VK_CHECK(vkResetCommandBuffer(cmd, 0));

    VkCommandBufferBeginInfo cmdBeginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    VK_CHECK(vkBeginCommandBuffer(cmd, &cmdBeginInfo));

    // NOTE: Can these be moved out?
    VkImageMemoryBarrier2 colorImageBarrier = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask = 0,
        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout        = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image            = ctx.swapchainImages[ctx.imageIndex],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1},
    };
    VkImageMemoryBarrier2 depthImageBarrier = {
        .sType        = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .dstStageMask  = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask    = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout        = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .image            = ctx.depthImage,
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT |
                                           VK_IMAGE_ASPECT_STENCIL_BIT, .levelCount = 1,
                             .layerCount = 1},
    };

    // NOTE: extra copy, but its ok
    VkImageMemoryBarrier2 imageMemoryBarriers[] = {colorImageBarrier,
                                                   depthImageBarrier};

    VkDependencyInfo barrierDependencyInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 2,
        .pImageMemoryBarriers    = imageMemoryBarriers,
    };

    // inserts the barriers into the command buffer
    vkCmdPipelineBarrier2(cmd, &barrierDependencyInfo);

    VkRenderingAttachmentInfo colorAttachmentInfo = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = ctx.swapchainImageViews[ctx.imageIndex],
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = {.color = {{0.0f, 0.0f, 0.2f, 1.0f}}},
    };

    VkRenderingAttachmentInfo depthAttachmentInfo = {
        .sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView   = ctx.depthImageView,
        .imageLayout = VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL,
        .loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp     = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue  = {.depthStencil = {1.0f, 0}},
    };

    VkRenderingInfo renderingInfo = {
        .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea           = {.extent = {.width = w, .height = h}},
        .layerCount           = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments    = &colorAttachmentInfo,
        .pDepthAttachment     = &depthAttachmentInfo,
    };

    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport = {
        .width = (f32)w, .height = (f32)h, .minDepth = 0.0f, .maxDepth = 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor = {
        .extent = {.width = w, .height = h}
    };
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, ctx.pipeline);
    VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &ctx.vertexBuffer, &vertexOffset);
    vkCmdBindIndexBuffer(cmd, ctx.vertexBuffer,
                         sizeof(Vertex) * ctx.vertexCount,
                         VK_INDEX_TYPE_UINT16);

    vkCmdPushConstants(cmd, ctx.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(VkDeviceAddress),
                       &ctx.shaderDataBuffers[ctx.frameIndex]);

    vkCmdDrawIndexed(cmd, ctx.indexCount, 1, 0, 0, 0);

    vkCmdEndRendering(cmd);

    VkImageMemoryBarrier2 barrierPresent = {
        .sType            = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask     = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .srcAccessMask    = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstStageMask     = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        .dstAccessMask    = 0,
        .oldLayout        = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout        = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .image            = ctx.swapchainImages[ctx.imageIndex],
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .levelCount = 1,
                             .layerCount = 1},
    };

    VkDependencyInfo barrierPresentDependencyInfo = {
        .sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers    = &barrierPresent,
    };

    vkCmdPipelineBarrier2(cmd, &barrierPresentDependencyInfo);
    vkEndCommandBuffer(cmd);

    // submit command buffer
    VkPipelineStageFlags waitStages =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = 1,
        .pWaitSemaphores      = &ctx.presentSemaphores[ctx.frameIndex],
        .pWaitDstStageMask    = &waitStages,
        .commandBufferCount   = 1,
        .pCommandBuffers      = &cmd,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores    = &ctx.renderSemaphores[ctx.imageIndex],
    };
    VK_CHECK(
        vkQueueSubmit(ctx.queue, 1, &submitInfo, ctx.fences[ctx.frameIndex]));

    ctx.frameIndex = (ctx.frameIndex + 1) % FRAMES_IN_FLIGHT;
    // present image

    VkPresentInfoKHR presentInfo = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                    .waitSemaphoreCount = 1,
                                    .pWaitSemaphores =
                                        &ctx.renderSemaphores[ctx.imageIndex],
                                    .swapchainCount = 1,
                                    .pSwapchains    = &ctx.swapchain,
                                    .pImageIndices  = &ctx.imageIndex};

    // enqueue images for presentation after waiting for the render semaphore
    VK_CHECK(vkQueuePresentKHR(ctx.queue, &presentInfo));

    // poll events
    f32 dt   = (SDL_GetTicksNS() - lastTime) / 1000.f;
    lastTime = SDL_GetTicksNS();
    while (SDL_PollEvent(&ctx.sdl_event)) {
      SDL_Event* e = &ctx.sdl_event;

      if (e->type == SDL_EVENT_QUIT) {
        ctx.running = false;
        break;
      }
      // TODO: add rotation of object

      if (e->type == SDL_EVENT_MOUSE_WHEEL) {
        glm_vec3_adds(ctx.camPos, (f32)e->wheel.y * dt * 10.0f, ctx.camPos);
      }

      if (e->type == SDL_EVENT_WINDOW_RESIZED) {
        ctx.updateSwapchain = true;
      }
    }

    // update swapchain
    if (ctx.updateSwapchain) {
      ctx.updateSwapchain = false;
      vkDeviceWaitIdle(ctx.device);
      VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          ctx.GPU, ctx.surface, &ctx.surfaceCapabilities));
      swapchainCI.oldSwapchain = ctx.swapchain;
      swapchainCI.imageExtent  = (VkExtent2D){.width = w, .height = h};
      VK_CHECK(
          vkCreateSwapchainKHR(ctx.device, &swapchainCI, NULL, &ctx.swapchain));

      for (u32 i = 0; i < ctx.swapchainImageCount; ++i) {
        vkDestroyImageView(ctx.device, ctx.swapchainImageViews[i], NULL);
      }
      VK_CHECK(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain,
                                       &ctx.swapchainImageCount, NULL));
      // wapchainImages.resize(ctx.swapchainImageCount);
      VK_CHECK(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain,
                                       &ctx.swapchainImageCount,
                                       ctx.swapchainImages));
      // swapchainImageViews.resize(imageCount);
      for (u32 i = 0; i < ctx.swapchainImageCount; ++i) {
        VkImageViewCreateInfo viewCI = {
            .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image            = ctx.swapchainImages[i],
            .viewType         = VK_IMAGE_VIEW_TYPE_2D,
            .format           = ctx.imageFormat,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .levelCount = 1,
                                 .layerCount = 1}
        };
        VK_CHECK(vkCreateImageView(ctx.device, &viewCI, NULL,
                                   &ctx.swapchainImageViews[i]));
      }
      vkDestroySwapchainKHR(ctx.device, swapchainCI.oldSwapchain, NULL);
      vmaDestroyImage(ctx.allocator, ctx.depthImage, ctx.depthImageAllocation);
      vkDestroyImageView(ctx.device, ctx.depthImageView, NULL);

      s32 w, h;
      SDL_GetWindowSizeInPixels(ctx.sdl_window, &w, &h);
      depthImageCI.extent = (VkExtent3D){.width = w, .height = h, .depth = 1};
      VmaAllocationCreateInfo allocCI = {
          .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
          .usage = VMA_MEMORY_USAGE_AUTO};
      VK_CHECK(vmaCreateImage(ctx.allocator, &depthImageCI, &allocCI,
                              &ctx.depthImage, &ctx.depthImageAllocation,
                              NULL));
      VkImageViewCreateInfo viewCI = {
          .sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          .image            = ctx.depthImage,
          .viewType         = VK_IMAGE_VIEW_TYPE_2D,
          .format           = ctx.depthFormat,
          .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                               .levelCount = 1,
                               .layerCount = 1}
      };
      VK_CHECK(
          vkCreateImageView(ctx.device, &viewCI, NULL, &ctx.depthImageView));
    }
     
    ctx.running = false;
  }
  // DESTRUCTION!
  VK_CHECK(vkDeviceWaitIdle(ctx.device));
  for (s32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
    vkDestroyFence(ctx.device, ctx.fences[i], NULL);
    vkDestroySemaphore(ctx.device, ctx.presentSemaphores[i], NULL);
  }

  for (u32 i = 0; i < ctx.swapchainImageCount; ++i) {
    vkDestroySemaphore(ctx.device, ctx.renderSemaphores[i], NULL);
    vkDestroyImageView(ctx.device, ctx.swapchainImageViews[i], NULL);
  }
  vmaDestroyImage(ctx.allocator, ctx.depthImage, ctx.depthImageAllocation);
  vkDestroyImageView(ctx.device, ctx.depthImageView, NULL);

  vmaDestroyBuffer(ctx.allocator, ctx.vertexBuffer, ctx.vertexBufferAllocation);

  vkDestroyPipelineLayout(ctx.device, ctx.pipelineLayout, NULL);
  vkDestroyPipeline(ctx.device, ctx.pipeline, NULL);
  vkDestroySwapchainKHR(ctx.device, ctx.swapchain, NULL);
  vkDestroySurfaceKHR(ctx.instance, ctx.surface, NULL);
  vkDestroyCommandPool(ctx.device, ctx.commandPool, NULL);
  vkDestroyShaderModule(ctx.device, ctx.shaderModule, NULL);
  vmaDestroyAllocator(ctx.allocator);

  SDL_DestroyWindow(ctx.sdl_window);
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
  SDL_Quit();

  vkDestroyDevice(ctx.device, NULL);
  vkDestroyInstance(ctx.instance, NULL);

  return 0;
}
