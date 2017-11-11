// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Intel architecture-specified performance monitor registers.
// This has the benefit of providing the data in a usage-independent way.

#ifndef DEF_ARCH_EVENT
#define DEF_ARCH_EVENT(symbol, cpuid_ebx_bit, event, umask, flags, name, description) /* nothing */
#endif

// Architectural performance counters
// args:
// - symbol
// - CPUID.0AH.EBX bit position (uint32_t)
// - event (uint32_t)
// - umask (uint32_t)
// - flags (IPM_REG_FLAG_* values) (uint32_t)
// - short name (ideally max 24 chars for legibility in display in chrome)
// - description (if empty use "")

// These are also the "fixed" counters.
DEF_ARCH_EVENT(ARCH_UNHALTED_CORE_CYCLES,        0, 0x3c, 0x00, IPM_REG_FLAG_ARCH | IPM_REG_FLAG_FIXED1,    "Unhalted Core Cycles",      "")
DEF_ARCH_EVENT(ARCH_INSTRUCTIONS_RETIRED,        1, 0xc0, 0x00, IPM_REG_FLAG_ARCH | IPM_REG_FLAG_FIXED0,    "Instructions Retired",      "")
DEF_ARCH_EVENT(ARCH_UNHALTED_REFERENCE_CYCLES,   2, 0x3c, 0x01, IPM_REG_FLAG_ARCH | IPM_REG_FLAG_FIXED2,    "Unhalted Reference Cycles", "")

DEF_ARCH_EVENT(ARCH_LLC_REFERENCE,               3, 0x2e, 0x4f, IPM_REG_FLAG_ARCH, "LLC References",        "Last Level Cache")
DEF_ARCH_EVENT(ARCH_LLC_MISSES,                  4, 0x2e, 0x41, IPM_REG_FLAG_ARCH, "LLC Misses",            "Last Level Cache")
DEF_ARCH_EVENT(ARCH_BRANCH_INSTRUCTIONS_RETIRED, 5, 0xc4, 0x00, IPM_REG_FLAG_ARCH, "Branches Retired",      "Branch instructions retired")
DEF_ARCH_EVENT(ARCH_BRANCH_MISSES_RETIRED,       6, 0xc5, 0x00, IPM_REG_FLAG_ARCH, "Branch Misses Retired", "")

#undef DEF_ARCH_EVENT
