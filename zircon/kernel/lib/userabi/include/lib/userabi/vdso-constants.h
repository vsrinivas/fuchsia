// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_VDSO_CONSTANTS_H_
#define ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_VDSO_CONSTANTS_H_

// This file is used both in the kernel and in the vDSO implementation.
// So it must be compatible with both the kernel and userland header
// environments.  It must use only the basic types so that struct
// layouts match exactly in both contexts.

#define VDSO_CONSTANTS_ALIGN 8
// The version string is based on a 40 character representation of a git
// hash. There is also a 4 byte 'git-' prefix, and possibly a 6 byte
// '-dirty' suffix. Let's be generous and use 64 bytes.
#define MAX_VERSION_STRING_SIZE 64

// The manifest for the constants size is currently...
// + 8 32-bit integers
// |++ max_num_cpus (1)
// |++ features (3)
// |++ cache lines sizes (2)
// |++ ticks to mono ratio (2)
// |
// + 3 64-bit integers
// | ticks_per_second (1)
// | physmem amount (1)
// | version_string_len
// |
// + max version string size (64 bytes)
//
#define VDSO_CONSTANTS_SIZE ((8 * 4) + (3 * 8) + MAX_VERSION_STRING_SIZE)

#ifndef __ASSEMBLER__

#include <stdint.h>
#include <zircon/time.h>

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
  // TODO(fxbug.dev/30418): This struct may need to grow over time as new features
  // are added and/or supported.  A mask may be needed to indicate which
  // bits are valid.
  struct {
    uint32_t cpu;

    // Total amount of debug registers available in the system.
    uint32_t hw_breakpoint_count;
    uint32_t hw_watchpoint_count;
  } features;

  // Number of bytes in a data cache line.
  uint32_t dcache_line_size;

  // Number of bytes in an instruction cache line.
  uint32_t icache_line_size;

  // Conversion factor for zx_ticks_get return values to seconds.
  zx_ticks_t ticks_per_second;

  // Ratio which relates ticks (zx_ticks_get) to clock monotonic (zx_clock_get_monotonic).
  // Specifically...
  //
  // ClockMono(ticks) = (ticks * N) / D
  //
  uint32_t ticks_to_mono_numerator;
  uint32_t ticks_to_mono_denominator;

  // Total amount of physical memory in the system, in bytes.
  uint64_t physmem;

  // Actual length of .version_string, not including the NUL terminator.
  uint64_t version_string_len;

  // A NUL-terminated UTF-8 string returned by zx_system_get_version_string.
  char version_string[MAX_VERSION_STRING_SIZE];
};

static_assert(VDSO_CONSTANTS_SIZE == sizeof(vdso_constants), "Need to adjust VDSO_CONSTANTS_SIZE");
static_assert(VDSO_CONSTANTS_ALIGN == alignof(vdso_constants),
              "Need to adjust VDSO_CONSTANTS_ALIGN");

#endif  // __ASSEMBLER__

#endif  // ZIRCON_KERNEL_LIB_USERABI_INCLUDE_LIB_USERABI_VDSO_CONSTANTS_H_
