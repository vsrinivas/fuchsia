// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "vm_priv.h"
#include <kernel/vm.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <trace.h>

#define LOCAL_TRACE MAX(VM_GLOBAL_TRACE, 0)

/* cheezy allocator that chews up space just after the end of the kernel mapping */

/* track how much memory we've used */
extern int _end;

uintptr_t boot_alloc_start = (uintptr_t)&_end;
uintptr_t boot_alloc_end = (uintptr_t)&_end;

void boot_alloc_reserve(uintptr_t start, size_t len) {
    uintptr_t end = ALIGN((start + len), PAGE_SIZE);

    // Adjust physical addresses to kernel memory map
    start += KERNEL_BASE;
    end += KERNEL_BASE;

    if (end >= boot_alloc_start) {
        if ((start > boot_alloc_start) &&
            ((start - boot_alloc_start) > (128 * 1024 * 1024))) {
            // if we've got 128MB of space, that's good enough
            // it's possible that the start may be *way* far up
            // (gigabytes) and there may not be space after it...
            return;
        }
        boot_alloc_start = boot_alloc_end = end;
    }
}

void* boot_alloc_mem(size_t len) {
    uintptr_t ptr;

    ptr = ALIGN(boot_alloc_end, 8);
    boot_alloc_end = (ptr + ALIGN(len, 8));

    LTRACEF("len %zu, ptr %p\n", len, (void*)ptr);

    return (void*)ptr;
}
