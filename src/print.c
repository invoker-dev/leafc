#include "print.h"
#include "vulkan/vulkan_core.h"
#include <stdio.h>

void print(const char* fmt, ...) {
  char    buf[2048];
  va_list args;
  va_start(args, fmt);
  stbsp_vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  fputs(buf, stderr); // NOTE: stderr ok?
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT             messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void*                                       pUserData) {

  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    print("validation layer: %s\n", pCallbackData->pMessage);
  }
  return VK_FALSE;
}
