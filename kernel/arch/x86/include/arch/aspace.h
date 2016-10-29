// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <arch/x86/mmu.h>
#include <kernel/spinlock.h>

__BEGIN_CDECLS

#define ARCH_ASPACE_MAGIC 0x41524153 // ARAS

struct arch_aspace {
    /* magic value for use-after-free detection */
    uint32_t magic;

    /* pointer to the translation table */
    paddr_t pt_phys;
    pt_entry_t *pt_virt;

    uint flags;

    /* range of address space */
    vaddr_t base;
    size_t size;

    /* cpus that are currently executing in this aspace
     * actually an mp_cpu_mask_t, but header dependencies. */
    volatile int active_cpus;

    /* Pointer to a bitmap::RleBitmap representing the range of ports
     * enabled in this aspace. */
    void *io_bitmap;
    spin_lock_t io_bitmap_lock;
};

__END_CDECLS

