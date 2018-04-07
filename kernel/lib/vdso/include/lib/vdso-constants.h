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

#define VDSO_CONSTANTS_ALIGN 8
// The build id is based on a 40 character representation of a git
// hash. There is also a 4 byte 'git-' prefix, and possibly a 6 byte
// '-dirty' suffix. Let's be generous and use 64 bytes.
#define MAX_BUILDID_SIZE 64
#define VDSO_CONSTANTS_SIZE (4 * 4 + 2 * 8 + MAX_BUILDID_SIZE)

#ifndef __ASSEMBLER__

#include <stdint.h>

// This struct contains constants that are initialized by the kernel
// once at boot time.  From the vDSO code's perspective, they are
// read-only data that can never change.  Hence, no synchronization is
// required to read them.
struct vdso_constants {

    // Maximum number of CPUs that might be online during the lifetime
    // of the booted system.
    uint32_t max_num_cpus;

    // Bit map indicating features.  For specific feature bits, see
    // zircon/features.h.
    // TODO(ZX-458): This struct may need to grow over time as new features
    // are added and/or supported.  A mask may be needed to indicate which
    // bits are valid.
    struct {
        uint32_t cpu;
    } features;

    // Number of bytes in a data cache line.
    uint32_t dcache_line_size;

    // Number of bytes in an instruction cache line.
    uint32_t icache_line_size;

    // Conversion factor for zx_ticks_get return values to seconds.
    zx_ticks_t ticks_per_second;

    // Total amount of physical memory in the system, in bytes.
    uint64_t physmem;

    // A build id of the system. Currently a non-null terminated ascii
    // representation of a git SHA.
    char buildid[MAX_BUILDID_SIZE];
};

static_assert(VDSO_CONSTANTS_SIZE == sizeof(vdso_constants),
              "Need to adjust VDSO_CONSTANTS_SIZE");
static_assert(VDSO_CONSTANTS_ALIGN == alignof(vdso_constants),
              "Need to adjust VDSO_CONSTANTS_ALIGN");

#endif // __ASSEMBLER__
