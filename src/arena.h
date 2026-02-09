#ifndef ARENA_H
#define ARENA_H
#include "types.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
  u16* buffer;
  u64  capacity;
  u64  offset;
} Mem_Arena;

Mem_Arena arena_create(u64 capacity);
void      arena_destroy(Mem_Arena* arena);
void*     arena_push(Mem_Arena*, u64 size);
void      arena_pop(Mem_Arena* arena, u64 size);
void      arena_clear(Mem_Arena* arena);

#define ARENA_PUSH_STRUCT(arena, T) (arena_push(arena, sizeof(T))) 
#define ARENA_PUSH_ARRAY(arena, T, n) (arena_push(arena, sizeof(T) * (n)))

#define ARENA_POP_STRUCT(arena, T) (arena_pop(arena, sizeof(T)))
#define ARENA_POP_ARRAY(arena, T, n) (arena_pop(arena, sizeof(T) * n))
#endif
