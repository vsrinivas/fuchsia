// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_MEMORY_H_
#define ZIRCON_KERNEL_PHYS_MEMORY_H_

#include <zircon/boot/image.h>
#include <zircon/compiler.h>

#include <cstddef>

// Parse the given ZBI to initialise the memory allocator with free ranges of memory.
//
// Panics on failure.
void InitMemory(const zbi_header_t* zbi);

// Attempt to allocate `size` bytes of memory with the given alignment.
//
// Return nullptr on failure.
void* AllocateMemory(size_t size, size_t alignment = __STDCPP_DEFAULT_NEW_ALIGNMENT__) __MALLOC;

// Return the given range of memory back to the allocator.
void FreeMemory(void* ptr, size_t size);

#endif  // ZIRCON_KERNEL_PHYS_MEMORY_H_
