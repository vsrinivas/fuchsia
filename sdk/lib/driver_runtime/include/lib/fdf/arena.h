// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_ARENA_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_ARENA_H_

#include <lib/fdf/types.h>

__BEGIN_CDECLS

// An arena which supports allocation of memory.
//
// The arena owns all the allocated memory. Allocated memory can be freed
// by calling |fdf_arena_free|, or will be freed when all references to
// the underlying runtime arena object are destroyed.
//
// |fdf_arena_create| will return a reference to a newly created runtime arena object.
// Passing an arena to |fdf_channel_write| will create and transfer a new reference to
// that same arena, and does not take ownership of your arena reference.
//
// # Thread safety
//
// Operations on arena objects are thread-safe.
//
// # Example
//
//   fdf_arena_t* arena;
//   zx_status_t status = fdf_arena_create(0, 'exam', 0, &arena);
//
//   // Allocate new blocks of memory.
//   void* addr1 = fdf_arena_allocate(arena, 0x1000);
//   void* addr2 = fdf_arena_allocate(arena, 0x2000);
//
//   // Use the allocated memory...
//
//   fdf_arena_destroy(arena);
typedef struct fdf_arena fdf_arena_t;

typedef uint32_t fdf_arena_tag_t;
// TODO(fxbug.dev/107594): support arena tags.

// Creates a FDF arena for allocating memory.
//
// |tag| provides a hint to the runtime so that it may be more efficient.
// For example, adjusting the size of the buffer backing the arena to the expected
// total size of allocations. It may also be surfaced in debug information.
//
// # Errors
//
// ZX_ERR_INVALID_ARGS: |options| is any value other than 0.
//
// ZX_ERR_NO_MEMORY: Failed due to a lack of memory.
zx_status_t fdf_arena_create(uint32_t options, fdf_arena_tag_t tag, fdf_arena_t** out_arena);

// Returns a pointer to allocated memory of size |bytes|. The memory is managed by the arena
// until it is freed by |fdf_arena_free|, or the arena is destroyed with |fdf_arena_destroy|.
void* fdf_arena_allocate(fdf_arena_t* arena, size_t bytes);

// Hints to the arena that the |ptr| previously allocated by |fdf_arena_allocate| may be reclaimed.
// Memory is not guaranteed to be reclaimed until |fdf_arena_destroy| is invoked.
// Asserts if the memory is not managed by the arena.
void fdf_arena_free(fdf_arena_t* arena, void* ptr);

// Returns whether the memory region consisting of [|ptr|, |ptr|+|num_bytes|) resides entirely
// within memory managed by the |arena|.
bool fdf_arena_contains(fdf_arena_t* arena, const void* ptr, size_t num_bytes);

// Destroys the reference to the underlying runtime arena object.
// If there are no more references to the arena, all memory associated with
// the arena will be freed.
void fdf_arena_destroy(fdf_arena_t* arena);

__END_CDECLS

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_ARENA_H_
