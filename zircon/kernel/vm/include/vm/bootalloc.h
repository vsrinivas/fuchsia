// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_VM_INCLUDE_VM_BOOTALLOC_H_
#define ZIRCON_KERNEL_VM_INCLUDE_VM_BOOTALLOC_H_

#include <sys/types.h>
#include <zircon/compiler.h>

// simple boot time allocator, used to carve off memory before
// the VM is completely up and running
extern "C" {
void boot_alloc_init();
void* boot_alloc_mem(size_t len) __MALLOC;
void boot_alloc_reserve(paddr_t phys, size_t len);
paddr_t boot_alloc_page_phys();

extern paddr_t boot_alloc_start;
extern paddr_t boot_alloc_end;

}  // extern "C"

#endif  // ZIRCON_KERNEL_VM_INCLUDE_VM_BOOTALLOC_H_
