// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <sys/types.h>
#include <zircon/types.h>

// The boot memory reservation system is a one-use early boot mechanism for
// a platform to mark certain ranges of physical space as occupied by something
// prior to adding arenas to the PMM.
//
// boot_reserve_init() must be called before adding the first pmm arena and
// boot_reserve_wire() should be called after the last arena is added to mark
// pages the reserved ranges intersect as WIRED.
//
// As the PMM arenas are added, the boot reserved ranges are consulted to make
// sure the pmm data structures do not overlap with any reserved ranges.

void boot_reserve_init();
void boot_reserve_wire();

zx_status_t boot_reserve_add_range(paddr_t pa, size_t len);

typedef struct {
    paddr_t pa;
    size_t len;
} reserve_range_t;

// Given a range, allocate a subrange within it of size alloc_len that does not intersect with
// any previously reserved ranges.
// Allocated range is upper aligned, starting with the highest base address to
// satisfy the requirements.
// Used by the PMM arena initialization code to allocate memory for itself.
zx_status_t boot_reserve_range_search(paddr_t range_pa, size_t range_len, size_t alloc_len,
                                      reserve_range_t* alloc_range);
