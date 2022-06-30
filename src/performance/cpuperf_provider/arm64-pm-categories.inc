// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Table of categories for configuring ARM64 Performance Monitor hardware.
// See ARM Architecture Reference Manual ARMv8, Chapter D5,
// "The Performance Monitors Extension".
//
// TODO(dje): Not all of these are supported on all arches.
// Ignore unsupported ones.

#ifndef DEF_FIXED_CATEGORY
#define DEF_FIXED_CATEGORY(symbol, name, events...)
#endif

#ifndef DEF_ARCH_CATEGORY
#define DEF_ARCH_CATEGORY(symbol, name, events...)
#endif

// Categories for fixed events
// Arguments:
// - symbol (must be unique among all categories)
// - name (must be unique among all categories,
//         cannot have any spaces: used in trace category name)
// - varargs list of register names (from event .inc files)

DEF_FIXED_CATEGORY(ARM64_CATEGORY_FIXED_CYCLE_COUNTER,
  "fixed:cycle_counter",
  FIXED_CYCLE_COUNTER)

// Categories for architecturally-defined events
// Arguments:
// - symbol (must be unique among all categories)
// - name (must be unique among all categories,
//         cannot have any spaces: used in trace category name)
// - varargs list of register names (from event .inc files)

DEF_ARCH_CATEGORY(ARM64_CATEGORY_ARCH_L1_ICACHE, "arch:l1_icache",
  ARCH_L1I_CACHE_REFILL,
  ARCH_L1I_TLB_REFILL)

DEF_ARCH_CATEGORY(ARM64_CATEGORY_ARCH_L1_DATA, "arch:l1_data",
  ARCH_L1D_CACHE,
  ARCH_L1D_CACHE_REFILL,
  ARCH_L1D_TLB_REFILL,
  ARCH_L1D_CACHE_ALLOCATE)

DEF_ARCH_CATEGORY(ARM64_CATEGORY_ARCH_L2_DATA, "arch:l2_data",
  ARCH_L2D_CACHE,
  ARCH_L2D_CACHE_REFILL,
  ARCH_L2D_CACHE_WB,
  ARCH_L2D_CACHE_ALLOCATE)

DEF_ARCH_CATEGORY(ARM64_CATEGORY_ARCH_EXCEPTIONS, "arch:exceptions",
  ARCH_EXC_TAKEN,
  ARCH_EXC_RETURN)

DEF_ARCH_CATEGORY(ARM64_CATEGORY_ARCH_INSTRUCTIONS, "arch:instructions",
  ARCH_LD_RETIRED,
  ARCH_ST_RETIRED,
  ARCH_INST_RETIRED,
  ARCH_INST_SPEC)

DEF_ARCH_CATEGORY(ARM64_CATEGORY_ARCH_BRANCHES, "arch:branches",
  ARCH_PC_WRITE_RETIRED,
  ARCH_BR_IMMED_RETIRED,
  ARCH_BR_RETURN_RETIRED,
  ARCH_BR_MIS_PRED,
  ARCH_BR_PRED)

DEF_ARCH_CATEGORY(ARM64_CATEGORY_ARCH_MISC_MEMORY, "arch:misc_memory",
  ARCH_UNALIGNED_LDST_RETIRED,
  ARCH_MEM_ACCESS,
  ARCH_MEMORY_ERROR,
  ARCH_TTBR_WRITE_RETIRED)

#undef DEF_FIXED_CATEGORY
#undef DEF_ARCH_CATEGORY
