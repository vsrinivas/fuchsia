// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <kernel/cmdline.h>
#include <kernel/vm.h>

#include <libfdt.h>
#include <arch/arm64/platform.h>

extern ulong lk_boot_args[4];

static paddr_t ramdisk_start_phys = 0;
static paddr_t ramdisk_end_phys = 0;

// Reads Linux device tree to initialize command line and return ramdisk location
void read_device_tree(void** ramdisk_base, size_t* ramdisk_size) {
    *ramdisk_base = NULL;
    *ramdisk_size = 0;

    void* fdt = paddr_to_kvaddr(lk_boot_args[0]);
    if (!fdt) {
        printf("%s: could not find device tree\n", __FUNCTION__);
        return;
    }

    if (fdt_check_header(fdt) < 0) {
        printf("%s fdt_check_header failed\n", __FUNCTION__);
        return;
    }

    int offset = fdt_path_offset(fdt, "/chosen");
    if (offset < 0) {
        printf("%s: fdt_path_offset(/chosen) failed\n", __FUNCTION__);
        return;
    }

    int length;
    const char* bootargs = fdt_getprop(fdt, offset, "bootargs", &length);
    if (bootargs) {
        printf("kernel command line: %s\n", bootargs);
        cmdline_init(bootargs);
    }

    const void* ptr = fdt_getprop(fdt, offset, "linux,initrd-start", &length);
    if (ptr) {
        if (length == 4) {
            ramdisk_start_phys = fdt32_to_cpu(*(uint32_t *)ptr);
        } else if (length == 8) {
            ramdisk_start_phys = fdt64_to_cpu(*(uint64_t *)ptr);
        }
    }
    ptr = fdt_getprop(fdt, offset, "linux,initrd-end", &length);
    if (ptr) {
        if (length == 4) {
            ramdisk_end_phys = fdt32_to_cpu(*(uint32_t *)ptr);
        } else if (length == 8) {
            ramdisk_end_phys = fdt64_to_cpu(*(uint64_t *)ptr);
        }
    }

    if (ramdisk_start_phys && ramdisk_end_phys) {
        *ramdisk_base = paddr_to_kvaddr(ramdisk_start_phys);
        size_t length = ramdisk_end_phys - ramdisk_start_phys;
        *ramdisk_size = (length + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }
}

void platform_preserve_ramdisk(void) {
    if (!ramdisk_start_phys || !ramdisk_end_phys) {
        return;
    }

    struct list_node list = LIST_INITIAL_VALUE(list);
    size_t pages = (ramdisk_end_phys - ramdisk_start_phys + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t actual = pmm_alloc_range(ramdisk_start_phys, pages, &list);
    if (actual != pages) {
        panic("unable to reserve ramdisk memory range\n");
    }

    // mark all of the pages we allocated as WIRED
    vm_page_t *p;
    list_for_every_entry(&list, p, vm_page_t, free.node) {
        p->state = VM_PAGE_STATE_WIRED;
    }
}

