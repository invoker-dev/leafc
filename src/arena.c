#include "arena.h"
#include "print.h"
#include <stdlib.h>

// TODO: make a real implementation

Arena arena_create(u64 size) {
  Arena a = (Arena){
      .size   = size,
      .buffer = malloc(size),
      .offset = 0,
  };
  return a;
}

void arena_destroy(Arena* arena) { free(arena->buffer); }

// aligns allocations to make CPU happy
u64 align(u64 pointer, u64 alignment) {
  u64 added_padding = alignment - 1;
  u64 mask          = ~(added_padding);
  return (pointer + added_padding) & mask;
}

void* arena_alloc(Arena* arena, u64 size, u64 alignment) {
  u64 current_ptr  = (u64)arena->buffer + arena->offset;
  u64 offset       = align(current_ptr, alignment);
  offset          -= (u64)arena->buffer; // get back to relative offset

  if ((offset + size) <= arena->size) {
    void* ptr      = &arena->buffer[offset];
    arena->offset += offset + size;
    return ptr;
  }
  print("\n!!!arena out of memory!!!\n");
  return NULL;
}

void arena_free(Arena* arena, u64 size) {
  arena->offset -= size;
}
