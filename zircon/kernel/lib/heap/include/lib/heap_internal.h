// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_HEAP_INCLUDE_LIB_HEAP_INTERNAL_H_
#define ZIRCON_KERNEL_LIB_HEAP_INCLUDE_LIB_HEAP_INTERNAL_H_

#include <lib/cmpctmalloc.h>
#include <stddef.h>
#include <sys/types.h>
#include <zircon/compiler.h>

// internal apis used by the heap implementation to get/return pages to the VM
void* heap_page_alloc(size_t pages) TA_REQ(TheHeapLock::Get());
void heap_page_free(void* ptr, size_t pages) TA_REQ(TheHeapLock::Get());

#endif  // ZIRCON_KERNEL_LIB_HEAP_INCLUDE_LIB_HEAP_INTERNAL_H_
