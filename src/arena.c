#include "arena.h"
#include "print.h"
#include <stdlib.h>

#define ALIGN_UP_POW2(n, p) (((u64)(n) + (u64)(p) - 1)) & (~(u64)(p) - 1)

Mem_Arena arena_create(u64 capacity) {
  Mem_Arena arena = {0};
  arena.buffer    = (u16*)malloc(capacity);
  arena.capacity  = capacity;
  arena.offset    = 0;
  return arena;
}

void arena_destroy(Mem_Arena* arena) { free(arena); }

void* arena_push(Mem_Arena* arena, u64 size) {
  u64  align_offset = ALIGN_UP_POW2(arena->offset, sizeof(*arena->buffer));
  u16* memory       = &arena->buffer[arena->offset];

  u64 new_offset = align_offset + size;
  if (new_offset > arena->capacity) {
    return NULL;
  }

  arena->offset = new_offset;

  return memory;
}

void arena_pop(Mem_Arena* arena, u64 size) {
  size         = (arena->offset - size) > 0 ? size : 0;
  arena->offset -= size;
}
