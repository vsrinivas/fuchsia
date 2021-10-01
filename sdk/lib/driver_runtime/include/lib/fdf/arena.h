// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_ARENA_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_ARENA_H_

#include <lib/fdf/types.h>

__BEGIN_CDECLS

// Usage Notes:
//
// fdf_arena_t owns all the allocated data until the arena is destroyed.
//
// Example:
//   fdf_arena_t* arena;
//   fdf_status_t status = fdf_arena_create(&arena);
//
//   // Allocate new blocks of memory.
//   void* addr1 = fdf_arena_allocate(arena, 0x1000);
//   void* addr2 = fdf_arena_allocate(arena, 0x2000);
//
//   // Use the allocated memory...
//
//   fdf_arena_destroy(arena);
//
typedef struct fdf_arena fdf_arena_t;

fdf_status_t fdf_arena_create(uint32_t options, const char* tag, fdf_arena_t** arena);

void* fdf_arena_allocate(fdf_arena_t* arena, size_t bytes);

// No-op for initial implementation.
void* fdf_arena_free(fdf_arena_t* arena, void* data);

// Returns true if the memory region consisting of [|data|, |data|+|num_bytes|) resides entirely
// within memory managed by the |arena|.
bool fdf_arena_contains(fdf_arena_t* arena, const void* data, size_t num_bytes);

// Frees all memory associated with the arena.
void fdf_arena_destroy(fdf_arena_t* arena);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_ARENA_H_
