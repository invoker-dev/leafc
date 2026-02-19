#ifndef ARENA_H
#define ARENA_H
#include "types.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  u8* buffer;
  u64  capacity;
  u64  offset;
} MemoryArena;

MemoryArena arena_create(u64 capacity);
void      arena_destroy(MemoryArena* arena);
void*     arena_push(MemoryArena*, u64 size);
void      arena_pop(MemoryArena* arena, u64 size);
// void      arena_clear(Mem_Arena* arena);

#define ARENA_PUSH_STRUCT(arena, T) (arena_push(arena, sizeof(T))) 
#define ARENA_PUSH_ARRAY(arena, T, n) (arena_push(arena, sizeof(T) * (n)))

#define ARENA_POP_STRUCT(arena, T) (arena_pop(arena, sizeof(T)))
#define ARENA_POP_ARRAY(arena, T, n) (arena_pop(arena, sizeof(T) * n))
#endif
