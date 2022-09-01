// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include "ftl_private.h"

// Wrapper for malloc()
void* FsMalloc(size_t size) {
  void* mem = malloc(size);

  return mem;
}

// Wrapper for calloc()
void* FsCalloc(size_t nitems, size_t size) {
  void* mem = calloc(nitems, size);

  return mem;
}

// Wrapper for aalloc()
void* FsAalloc(size_t size) {
  void* mem = aalloc(size);

  return mem;
}

// Wrapper for free()
void FsFree(void* ptr) { free(ptr); }

// Wrapper for free_clear()
void FsFreeClear(void* ptr) { free_clear(ptr); }

// Wrapper for afree_clear()
void FsAfreeClear(void* ptr) { afree_clear(ptr); }
