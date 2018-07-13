// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is based on zircon/kernel/arch/x86/include/arch/x86/feature.h.
// TODO(dje): As with generic elf, dwarf, et.al, move to application
// independent library.

#pragma once

#include <cstdint>
#include <cstdio>

#include "lib/fxl/logging.h"

namespace debugserver {

// cpu vendors
enum x86_vendor_list { X86_VENDOR_UNKNOWN, X86_VENDOR_INTEL, X86_VENDOR_AMD };

struct x86_model_info {
  uint8_t processor_type;
  uint8_t family;
  uint8_t model;
  uint8_t stepping;

  uint16_t display_family;
  uint8_t display_model;
};

const x86_model_info* x86_get_model();

#define MAX_SUPPORTED_CPUID (0x17)
#define MAX_SUPPORTED_CPUID_EXT (0x80000008)

struct x86_cpuid_leaf {
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
};

enum x86_cpuid_leaf_num {
  X86_CPUID_BASE = 0,
  X86_CPUID_MODEL_FEATURES = 0x1,
  X86_CPUID_TOPOLOGY = 0xb,
  X86_CPUID_XSAVE = 0xd,
  X86_CPUID_PT = 0x14,

  X86_CPUID_EXT_BASE = 0x80000000,
  X86_CPUID_ADDR_WIDTH = 0x80000008,
};

struct x86_cpuid_bit {
  enum x86_cpuid_leaf_num leaf_num;
  uint8_t word;
  uint8_t bit;
};

void x86_feature_init();

const x86_cpuid_leaf* x86_get_cpuid_leaf(enum x86_cpuid_leaf_num leaf);

// Retrieve the specified subleaf.  This function is not cached.
// Returns false if leaf num is invalid.
bool x86_get_cpuid_subleaf(enum x86_cpuid_leaf_num, uint32_t, x86_cpuid_leaf*);

bool x86_feature_test(x86_cpuid_bit bit);

// TODO(dje): Switch to iostreams later, maybe.
void x86_feature_debug(FILE* out);

#define X86_CPUID_BIT(leaf, word, bit)                       \
  (::debugserver::x86_cpuid_bit) {                           \
    (::debugserver::x86_cpuid_leaf_num)(leaf), (word), (bit) \
  }

// add feature bits to test here
#define X86_FEATURE_SSE3 X86_CPUID_BIT(0x1, 2, 0)
#define X86_FEATURE_SSSE3 X86_CPUID_BIT(0x1, 2, 9)
#define X86_FEATURE_SSE4_1 X86_CPUID_BIT(0x1, 2, 19)
#define X86_FEATURE_SSE4_2 X86_CPUID_BIT(0x1, 2, 20)
#define X86_FEATURE_TSC_DEADLINE X86_CPUID_BIT(0x1, 2, 24)
#define X86_FEATURE_AESNI X86_CPUID_BIT(0x1, 2, 25)
#define X86_FEATURE_XSAVE X86_CPUID_BIT(0x1, 2, 26)
#define X86_FEATURE_AVX X86_CPUID_BIT(0x1, 2, 28)
#define X86_FEATURE_RDRAND X86_CPUID_BIT(0x1, 2, 30)
#define X86_FEATURE_FPU X86_CPUID_BIT(0x1, 3, 0)
#define X86_FEATURE_MMX X86_CPUID_BIT(0x1, 3, 23)
#define X86_FEATURE_FXSR X86_CPUID_BIT(0x1, 3, 24)
#define X86_FEATURE_SSE X86_CPUID_BIT(0x1, 3, 25)
#define X86_FEATURE_SSE2 X86_CPUID_BIT(0x1, 3, 26)
#define X86_FEATURE_TSC_ADJUST X86_CPUID_BIT(0x7, 1, 1)
#define X86_FEATURE_AVX2 X86_CPUID_BIT(0x7, 1, 5)
#define X86_FEATURE_SMEP X86_CPUID_BIT(0x7, 1, 7)
#define X86_FEATURE_RDSEED X86_CPUID_BIT(0x7, 1, 18)
#define X86_FEATURE_SMAP X86_CPUID_BIT(0x7, 1, 20)
#define X86_FEATURE_PT X86_CPUID_BIT(0x7, 1, 25)
#define X86_FEATURE_PKU X86_CPUID_BIT(0x7, 2, 3)
#define X86_FEATURE_SYSCALL X86_CPUID_BIT(0x80000001, 3, 11)
#define X86_FEATURE_NX X86_CPUID_BIT(0x80000001, 3, 20)
#define X86_FEATURE_HUGE_PAGE X86_CPUID_BIT(0x80000001, 3, 26)
#define X86_FEATURE_RDTSCP X86_CPUID_BIT(0x80000001, 3, 27)
#define X86_FEATURE_INVAR_TSC X86_CPUID_BIT(0x80000007, 3, 8)

// topology

#define X86_TOPOLOGY_INVALID 0
#define X86_TOPOLOGY_SMT 1
#define X86_TOPOLOGY_CORE 2

struct x86_topology_level {
  // The number of bits to right shift to identify the next-higher topological
  // level.
  uint8_t right_shift;
  // The type of relationship this level describes (hyperthread/core/etc).
  uint8_t type;
};

/**
 * @brief Fetch the topology information for the given level.
 *
 * This interface is uncached.
 *
 * @param level The level to retrieve info for.  Should initially be 0 and
 * incremented with each call.
 * @param info The structure to populate with the discovered information
 *
 * @return true if the requested level existed (and there may be higher levels).
 * @return false if the requested level does not exist (and no higher ones do).
 */
bool x86_topology_enumerate(uint8_t level, x86_topology_level* info);

}  // namespace debugserver
