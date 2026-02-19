#include "arena.h"
#include "print.h"
#include <stdlib.h>

// aligns n to the alignment p
#define ALIGN_UP_POW2(n, p) (((u64)(n) + (u64)(p) - 1)) & (~(u64)(p) - 1)

MemoryArena arena_create(u64 capacity) {
  MemoryArena arena = {0};
  arena.buffer    = (u8*)malloc(capacity);
  arena.capacity  = capacity;
  arena.offset    = 0;
  return arena;
}

void arena_destroy(MemoryArena* arena) { free(arena->buffer); }

void* arena_push(MemoryArena* arena, u64 size) {
  u64 align_offset = ALIGN_UP_POW2(arena->offset, sizeof(u64));

  u64 new_offset = align_offset + size;
  if (new_offset > arena->capacity) {
    return NULL;
  }

  void* memory  = &arena->buffer[align_offset];
  arena->offset = new_offset;

  return memory;
}

void arena_pop(MemoryArena* arena, u64 size) {
  if (size > arena->offset) {
    arena->offset = 0;
  } else {
    arena->offset -= size;
  }
}
