// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

// This file is used both in the kernel and in the vDSO implementation.
// So it must be compatible with both the kernel and userland header
// environments.  It must use only the basic types so that struct
// layouts match exactly in both contexts.

#define VDSO_CONSTANTS_SIZE (4 * 4 + 2 * 8)
#define VDSO_CONSTANTS_ALIGN 8

#ifndef ASSEMBLY

#include <stdint.h>

// This struct contains constants that are initialized by the kernel
// once at boot time.  From the vDSO code's perspective, they are
// read-only data that can never change.  Hence, no synchronization is
// required to read them.
struct vdso_constants {

    // Maximum number of CPUs that might be online during the lifetime
    // of the booted system.
    uint32_t max_num_cpus;

    // Number of bytes in a data cache line.
    uint32_t dcache_line_size;

    // Number of bytes in an instruction cache line.
    uint32_t icache_line_size;

    // Conversion factor for mx_ticks_get return values to seconds.
    uint64_t ticks_per_second;

    // Total amount of physical memory in the system, in bytes.
    uint64_t physmem;
};

static_assert(VDSO_CONSTANTS_SIZE == sizeof(vdso_constants),
              "Need to adjust VDSO_CONSTANTS_SIZE");
static_assert(VDSO_CONSTANTS_ALIGN == alignof(vdso_constants),
              "Need to adjust VDSO_CONSTANTS_ALIGN");

#endif // ASSEMBLY
