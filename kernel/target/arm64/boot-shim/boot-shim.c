// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "boot-shim.h"
#include "debug.h"

#define ROUNDUP(a, b) (((a) + ((b)-1)) & ~((b)-1))

static void fail(const char* message) {
    uart_puts(message);
    while (1) {}
}

uint64_t boot_shim(void* device_tree, zircon_kernel_t* kernel) {
    uart_puts("boot_shim: hi there!\n");

    // sanity check the bootdata headers
    // it must start with a container record followed by a kernel record
    if (kernel->hdr_file.type != BOOTDATA_CONTAINER || kernel->hdr_file.extra != BOOTDATA_MAGIC ||
        kernel->hdr_file.magic != BOOTITEM_MAGIC || kernel->hdr_kernel.type != BOOTDATA_KERNEL ||
        kernel->hdr_kernel.magic != BOOTITEM_MAGIC) {
        fail("zircon_kernel_t sanity check failed\n");
    }

    uintptr_t kernel_base;
    uint32_t bootdata_size = kernel->hdr_file.length + sizeof(bootdata_t);
    uint32_t kernel_size = kernel->hdr_kernel.length + 2 * sizeof(bootdata_t);

    if (bootdata_size > kernel_size) {
        // we have more bootdata following the kernel.
        // we must relocate the kernel after the rest of the bootdata.

        // round up to align new kernel location
        bootdata_size = ROUNDUP(bootdata_size, KERNEL_ALIGN);
        kernel_base = (uintptr_t)kernel + bootdata_size;

        // poor-man's memcpy, since we don't have a libc in here
        uint64_t* src = (uint64_t*)kernel;
        uint64_t* dest = (uint64_t*)kernel_base;
        uint64_t* end = (uint64_t*)((void*)src + kernel_size);
        end = (uint64_t*)BOOTDATA_ALIGN((uintptr_t)end);

        while (src < end) {
            *dest++ = *src++;
        }
    } else {
        kernel_base = (uintptr_t)kernel;
    }

    // return kernel entry point address
    return kernel_base + kernel->data_kernel.entry64;
}
