#ifndef PRINT_H
#define PRINT_H
#include "vulkan/vulkan_core.h"
#include <stb_sprintf.h>
#include <stdarg.h>
#include <stdio.h>
void print(const char* fmt, ...);

extern VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT             messageType,
              const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
              void*                                       pUserData);
#endif
