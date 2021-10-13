// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdf/arena.h>

#include "src/devices/bin/driver_runtime/arena.h"

// fdf_arena_t interface

__EXPORT fdf_status_t fdf_arena_create(uint32_t options, const char* tag, size_t tag_len,
                                       fdf_arena_t** out_arena) {
  return fdf_arena::Create(options, tag, tag_len, out_arena);
}

__EXPORT void* fdf_arena_allocate(fdf_arena_t* arena, size_t bytes) {
  return arena->Allocate(bytes);
}

__EXPORT void* fdf_arena_free(fdf_arena_t* arena, void* data) { return arena->Free(data); }

__EXPORT bool fdf_arena_contains(fdf_arena_t* arena, const void* data, size_t num_bytes) {
  return arena->Contains(data, num_bytes);
}

__EXPORT void fdf_arena_destroy(fdf_arena_t* arena) { arena->Destroy(); }
