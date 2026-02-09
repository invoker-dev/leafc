#include "SDL3/SDL_init.h"
#include "SDL3/SDL_video.h"
#include "arena.h"
#include "print.h"
#include "types.h"
#include "vulkan/vulkan_core.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <stb_sprintf.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define VOLK_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <volk.h>
#include <vulkan/vk_enum_string_helper.h>

#define vk_check(result)                                                       \
  {                                                                            \
    if (result != VK_SUCCESS) {                                                \
      print("Vulkan call at %d returned an error: %s\n", __LINE__,             \
            string_VkResult(result));                                          \
    }                                                                          \
  }

#define sdl_check(result)                                                      \
  {                                                                            \
    if (result == 0) {                                                         \
      print("SDL call returned an error: %s\n", SDL_GetError());               \
    }                                                                          \
  }

// TODO: make dynamic array impl
// TODO: fix arena
// TODO: 1. manual vertex data, cube
// TODO: 2. load gltf / some type of vertex data

typedef struct {

  Arena      arena;
  VkInstance instance;

  VkPhysicalDevice*        GPUs;
  u32                      GPUIndex;
  VkQueueFamilyProperties* queueFamilies;
  u32                      queueFamilyIndex;

  VkDevice device; // GPU Driver

  VmaAllocator allocator;

  VkSurfaceKHR surface;
  VkFormat     imageFormat;

  VkSwapchainKHR swapchain;
  VkImage*       swapchainImages;
  VkImageView*   swapchainImageViews;

  VkImage       depthImage;
  VkImageView   depthImageView;
  VkFormat      depthFormat;
  VmaAllocation depthImageAllocation;

  SDL_Window* sdl_window;

} VulkanContext;

int main(void) {

  VulkanContext ctx = {0};

  ctx.arena = arena_create(1024 * 1024 * 1024);

  sdl_check(SDL_Init(SDL_INIT_VIDEO));
  sdl_check(SDL_Vulkan_LoadLibrary(NULL));

  if (volkInitialize() != VK_SUCCESS) {
    return 1;
  }

  // NOTE: maybe move this somewhere else?

  VkApplicationInfo appInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                               .pApplicationName = "leafc",
                               .apiVersion       = VK_API_VERSION_1_3};

  // fetch platform specific extensions
  u32                sdlExtensionCount = 0;
  const char* const* sdlExtensions =
      SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);

  // WARN: This is bad, fix dynamic arrays
  u32          totalExtensionCount = sdlExtensionCount + 1;
  const char** extensions =
      arena_alloc_array(&ctx.arena, const char*, sdlExtensionCount + 1); // <-
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
      arena_alloc_array(&ctx.arena, VkLayerProperties, layerCount);
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

  vk_check(vkCreateInstance(&instanceCI, NULL, &ctx.instance));
  volkLoadInstance(ctx.instance);

  // Device selection
  u32 deviceCount = 0;
  vk_check(vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, NULL));
  ctx.GPUs = arena_alloc_array(&ctx.arena, VkPhysicalDevice, deviceCount);
  vk_check(vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, ctx.GPUs));

  VkPhysicalDeviceProperties2 deviceProperties = {0};
  deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  vkGetPhysicalDeviceProperties2(ctx.GPUs[ctx.GPUIndex], &deviceProperties);
  print("selected device: %s\n", deviceProperties.properties.deviceName);

  // Queues
  u32 queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.GPUs[ctx.GPUIndex],
                                           &queueFamilyCount, NULL);
  VkQueueFamilyProperties* queueFamilies =
      arena_alloc_array(&ctx.arena, VkQueueFamilyProperties, queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(ctx.GPUs[ctx.GPUIndex],
                                           &queueFamilyCount, queueFamilies);
  // find a queue with graphics support
  for (u32 i = 0; i < queueFamilyCount; ++i) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      ctx.queueFamilyIndex = i;
      break;
    }
  }

  // check if queue supports presentation
  sdl_check(SDL_Vulkan_GetPresentationSupport(
      ctx.instance, ctx.GPUs[ctx.GPUIndex], ctx.queueFamilyIndex));

  const f32               queueFamilyPriorities = 1.0f;
  VkDeviceQueueCreateInfo queueCI               = {
                    .sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                    .queueFamilyIndex = ctx.queueFamilyIndex,
                    .queueCount       = 1,
                    .pQueuePriorities = &queueFamilyPriorities,
  };

  // WARN: make this an array if more extensions needed
  const char* const deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

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

  VkDeviceCreateInfo deviceCI = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                 .pNext = &enabledVk13Features,
                                 .queueCreateInfoCount    = 1,
                                 .pQueueCreateInfos       = &queueCI,
                                 .enabledExtensionCount   = 1,
                                 .ppEnabledExtensionNames = deviceExtensions,
                                 .pEnabledFeatures = &enabledVk10Features};

  vk_check(
      vkCreateDevice(ctx.GPUs[ctx.GPUIndex], &deviceCI, NULL, &ctx.device));

  VmaAllocatorCreateInfo allocatorCI = {
      .flags            = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice   = ctx.GPUs[ctx.GPUIndex],
      .device           = ctx.device,
      .pVulkanFunctions = NULL,
      .instance         = ctx.instance};

  vk_check(vmaCreateAllocator(&allocatorCI, &ctx.allocator));

  // SDL !!!

  ctx.sdl_window = SDL_CreateWindow("leafc", 1280u, 720u,
                                    SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!ctx.sdl_window) {
    print("could not create SDL window: %s\n", SDL_GetError());
  }

  // request a surface from SDL
  sdl_check(SDL_Vulkan_CreateSurface(ctx.sdl_window, ctx.instance, NULL,
                                     &ctx.surface));

  VkSurfaceCapabilitiesKHR surfaceCapabilities = {0};
  vk_check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
      ctx.GPUs[ctx.GPUIndex], ctx.surface, &surfaceCapabilities));

  if (surfaceCapabilities.currentExtent.width == UINT32_MAX ||
      surfaceCapabilities.currentExtent.height == UINT32_MAX) {
    s32 w, h;
    surfaceCapabilities.currentExtent.width =
        SDL_GetWindowSizeInPixels(ctx.sdl_window, &w, &h);
    surfaceCapabilities.currentExtent.width  = w;
    surfaceCapabilities.currentExtent.height = h;
  }

  // swapchain
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

  vk_check(
      vkCreateSwapchainKHR(ctx.device, &swapchainCI, NULL, &ctx.swapchain));

  u32 imageCount = 0;
  vk_check(
      vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &imageCount, NULL));
  ctx.swapchainImages = arena_alloc_array(&ctx.arena, VkImage, imageCount);
  vk_check(vkGetSwapchainImagesKHR(ctx.device, ctx.swapchain, &imageCount,
                                   ctx.swapchainImages));
  ctx.swapchainImageViews =
      arena_alloc_array(&ctx.arena, VkImageView, imageCount);

  // check which depth format GPU supports
  const VkFormat depthFormatList[] = {VK_FORMAT_D32_SFLOAT_S8_UINT,
                                      VK_FORMAT_D24_UNORM_S8_UINT};

  for (int i = 0; i < 2; ++i) {
    VkFormatProperties2 formatProperties = {0};
    formatProperties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    vkGetPhysicalDeviceFormatProperties2(ctx.GPUs[ctx.GPUIndex],
                                         depthFormatList[i], &formatProperties);
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
  vk_check(vmaCreateImage(ctx.allocator, &depthImageCI, &allocCI,
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

  vk_check(vkCreateImageView(ctx.device, &depthImageViewCI, NULL,
                             &ctx.depthImageView));

  // vertex data !


  return 0;
}
