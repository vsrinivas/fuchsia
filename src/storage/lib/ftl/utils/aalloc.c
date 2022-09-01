// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <zircon/assert.h>

#include "ftl_private.h"

#ifndef CACHE_LINE_SIZE
#error CACHE_LINE_SIZE is undefined
#endif

// Free allocated memory and clear pointer to it.
//
// Input: alloc_ptr_ptr = ptr to variable holding allocated address.
void free_clear(void* alloc_ptr_ptr) {
  void** allocpp = alloc_ptr_ptr;

  // Free the allocated memory.
  ZX_DEBUG_ASSERT(*allocpp);
  free(*allocpp);

  // Clear the allocation pointer/flag.
  *allocpp = NULL;
}

// Allocate cache line size aligned memory.
//
// Input: size = amount of memory to allocate in bytes.
// Returns: Pointer to aligned memory block on success, else NULL.
void* aalloc(size_t size) {
#if CACHE_LINE_SIZE <= 8
  return malloc(size);
#else
  uintptr_t malloc_addr, fs_alloc_addr;

  // Increase size for malloc request to allow for alignment and for
  // storage of start of malloc-ed memory.
  size += sizeof(uintptr_t) + CACHE_LINE_SIZE - 1;

  // Allocate memory.
  malloc_addr = (uintptr_t)calloc(size, sizeof(ui8));
  if (malloc_addr == 0)
    return NULL;

  // Compute start of aligned memory block.
  fs_alloc_addr = (malloc_addr + sizeof(uintptr_t) + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);

  // Store start address immediately prior to aligned memory.
  *(uintptr_t*)(fs_alloc_addr - sizeof(uintptr_t)) = malloc_addr;

  // Return start of aligned memory.
  return (void*)fs_alloc_addr;
#endif
}

// Free allocated aligned memory and clear pointer to it.
//
// Input: aligned_ptr_addr = pointer to variable holding line-size aligned allocation address.
void afree_clear(void* aligned_ptr_addr) {
#if CACHE_LINE_SIZE <= 8
  free_clear(aligned_ptr_addr);
#else
  void*** aptr = aligned_ptr_addr;

  // Free allocated memory.
  free(*(*aptr - 1));

  // Clear input pointer.
  *aptr = 0;
#endif
}
