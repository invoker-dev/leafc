#ifndef SHADER_H
#define SHADER_H
#include "types.h"
#include <cglm/cglm.h>
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

typedef struct {
  mat4 projection;
  mat4 view;
  mat4 model[3];
  vec4 lightPos;
  u32  selected;
} ShaderData;

typedef struct {
  VmaAllocation   allocation;
  VkBuffer        buffer;
  VkDeviceAddress deviceAddress;
  void*           mapped;
} ShaderDataBuffer;

#endif
