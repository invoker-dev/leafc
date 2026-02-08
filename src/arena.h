#ifndef ARENA_H
#define ARENA_H
#include "types.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  u16* buffer;
  u64  size;
  u64  offset;
} Arena;

Arena arena_create(u64 size);
void  arena_destroy(Arena* arena);
void* arena_alloc(Arena* arena, u64 size, u64 alignment);
void  arena_free(Arena* arena, u64 size);

#define arena_alloc_array(arena, type, count)                                  \
  (type*)arena_alloc((arena), sizeof(type) * count, alignof(type))

#define arena_free_array(arena, type, count)                                  \
  (type*)arena_free((arena), sizeof(type) * count)

#define arena_alloc_struct(arena, type)                                        \
  (type*)arena_alloc((arena), sizeof(type), alignof(type))

#endif
