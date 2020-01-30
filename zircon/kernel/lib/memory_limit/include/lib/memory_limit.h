// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_MEMORY_LIMIT_INCLUDE_LIB_MEMORY_LIMIT_H_
#define ZIRCON_KERNEL_LIB_MEMORY_LIMIT_INCLUDE_LIB_MEMORY_LIMIT_H_

#include <sys/types.h>
#include <zircon/types.h>
#include <vm/pmm.h>

__BEGIN_CDECLS

// This library exists to calculate memory ranges to be used as arenas
// for the pmm based on a predefined memory limit. The limit is passed in
// MB via the kernel.memory-limit-mb cmdline argument. The library will
// calculate memory arenas based on provided ranges, reserved boot regions,
// and the limit provided and add those to the system when
// memory_limit_add_arenas is called.
//
// A system that has a single contiguous range of memory may look something
// like this with a couple reserved regions at the front. Limiting memory in
// this case is simple since we can shrink the range on the right.
// +-------------------------------------------------+
// |--|  ||                                          |
// +-------------------------------------------------+
// becomes
// +---------------------+
// |--|  ||              |
// +---------------------+
// A more complicated layout might be two contiguous memory ranges with reserved
// boot regions in each:
// +--------------------------------+  +-------------+
// |   |-|       |-----|            |  |      |--|   |
// +--------------------------------+  +-------------+
// In this case we would have to ensure reach reserved range is mantained but
// also keep it within a limit. It could end up like the following after we
// track front/back space in each range and adjust things as needed (front /
// back are noted by characters):
//    +------+       +---------------------+      +------+
//  f |  |-| | b   f |  |-----|            | b  f | |--| | b
//    +------+       +---------------------+      +------+
// Even more complicated layouts like the following also need to be handled:
// +--------+ +--------+ +---------------+  +--------+
// | |-|    | |   |--| | | |--|          |  |   ||   |
// +--------+ +--------+ +---------------+  +--------+
//
// In this case we would need to ensure we fit all four reserved regions in four
// separate ranges, then grow each one outward as the limit allowed.
//
// For the reasons above, the library makes multiple passes through the memory
// arenas to ensure all reserved regions are protected, and while doing so it
// tracks 'spare' memory nearby so that it can be added to the PMM if the
// limit provided facilitates it.

// Checks if a memory limit exists and initializes the lib bookkeeping.
//
// Returns ZX_OK on success, ZX_ERR_BAD_STATE if already initialized, or
// ERR_NOT_SUPPORTED if no memory limit was passed via kernel.memory-limit-mb
zx_status_t memory_limit_init();

// Adds a given range of memory to the memory allocator to use in
// sorting out memory arenas.
//
// @range_base: the start address of the range.
// @range_size: size of the range in bytes
// @arena_template: a structure containing the default values for flags,
// priority, and name used for arenas created by this function in the
// event of any failure conditions.
//
// Returns ZX_OK on completion, and ZX_ERR_INVALID_ARGS if parameters are
// invalid
zx_status_t memory_limit_add_range(uintptr_t range_base, size_t range_size,
                                   pmm_arena_info_t arena_template);

// Uses the ranges provided by memory_limit_add_range to calculate the
// acceptable memory arenas to fit within our imposed memory limitations
// while still including all required reserved boot regions.
//
// @arena_template: a structure containing the default values for flags,
// priority, and name used for arenas created by this function in the
// event of any failure conditions.
zx_status_t memory_limit_add_arenas(pmm_arena_info_t arena_template);

__END_CDECLS

#endif  // ZIRCON_KERNEL_LIB_MEMORY_LIMIT_INCLUDE_LIB_MEMORY_LIMIT_H_
