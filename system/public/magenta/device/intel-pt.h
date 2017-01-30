// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/types.h>
#include <stddef.h>

__BEGIN_CDECLS

#if !defined(__x86_64__)
#error "unsupported architecture"
#endif

// Bits in the IA32_RTIT_CTL MSR.
// These bits are writable by the user with ioctl_ipt_set_ctl_config.
// The driver will override a setting if it's unsafe (e.g., causes #gpf).
// TODO(dje): Append _MASK, add _BIT, _LEN.
#define IPT_CTL_CYC_EN (1ULL << 1)
#define IPT_CTL_OS_ALLOWED (1ULL << 2)
#define IPT_CTL_USER_ALLOWED (1ULL << 3)
#define IPT_CTL_POWER_EVENT_EN (1ULL << 4)
#define IPT_CTL_FUP_ON_PTW (1ULL << 5)
#define IPT_CTL_CR3_FILTER (1ULL << 7)
#define IPT_CTL_MTC_EN (1ULL << 9)
#define IPT_CTL_TSC_EN (1ULL << 10)
#define IPT_CTL_DIS_RETC (1ULL << 11)
#define IPT_CTL_PTW_EN (1ULL << 12)
#define IPT_CTL_BRANCH_EN (1ULL << 13)
#define IPT_CTL_MTC_FREQ (0xfULL << 14)
#define IPT_CTL_CYC_THRESH (0xfULL << 19)
#define IPT_CTL_PSB_FREQ (0xfULL << 24)
#define IPT_CTL_ADDR0 (0xfULL << 32)
#define IPT_CTL_ADDR1 (0xfULL << 36)
#define IPT_CTL_ADDR2 (0xfULL << 40)
#define IPT_CTL_ADDR3 (0xfULL << 44)

// Other bits in IA32_RTIT_CTL MSR, not writable with ioctl_ipt_set_ctl_config.
#define IPT_CTL_TRACE_EN (1ULL<<0)
#define IPT_CTL_FABRIC_EN (1ULL<<6)
#define IPT_CTL_TOPA (1ULL<<8)

// Masks for reading IA32_RTIT_STATUS.
#define IPT_STATUS_FILTER_EN (1ULL<<0)
#define IPT_STATUS_CONTEXT_EN (1ULL<<1)
#define IPT_STATUS_TRIGGER_EN (1ULL<<2)
#define IPT_STATUS_ERROR (1ULL<<4)
#define IPT_STATUS_STOPPED (1ULL<<5)

// Maximum number of address ranges that h/w may support.
#define IPT_MAX_NUM_ADDR_RANGES 4

// Valid ToPA entry sizes.
#define IPT_TOPA_MIN_SHIFT 12
#define IPT_TOPA_MAX_SHIFT 27

// These bits are for internal use

// Macros for building entries for the Table of Physical Addresses
#define IPT_TOPA_ENTRY_PHYS_ADDR(x) ((uint64_t)(x) & ~((1ULL<<12)-1))
#define IPT_TOPA_ENTRY_SIZE(size_log2) ((uint64_t)((size_log2) - 12) << 6)
#define IPT_TOPA_ENTRY_STOP (1ULL << 4)
#define IPT_TOPA_ENTRY_INT (1ULL << 2)
#define IPT_TOPA_ENTRY_END (1ULL << 0)

// Macros for extracting info from ToPA entries
#define IPT_TOPA_ENTRY_EXTRACT_PHYS_ADDR(e) \
  ((mx_paddr_t)((e) & ~((1ULL<<12)-1)))
#define IPT_TOPA_ENTRY_EXTRACT_SIZE(e) ((uint)((((e) >> 6) & 0xf) + 12))

// Arbitarily picked constants for ourselves.
// ToPA tables are 16KB in size (technically can be up to 256MB).
// A 16KB table provides 2047 non-END entries, so at the
// minimum can provide a capture buffer of just under 8MB.
#define IPT_TOPA_MAX_TABLE_ENTRIES 2048

// The PT register set.
// This is accessed via mtrace, but basically it's a regset.
typedef struct {
    uint64_t ctl;
    uint64_t status;
    uint64_t output_base;
    uint64_t output_mask_ptrs;
    uint64_t cr3_match;
    struct {
        uint64_t a,b;
    } addr_ranges[IPT_MAX_NUM_ADDR_RANGES];
} mx_x86_pt_regs_t;

// Two "modes" of tracing are supported:
// trace each cpu, regardless of what's running on it
#define IPT_MODE_CPUS 0
// trace specific threads
#define IPT_MODE_THREADS 1

__END_CDECLS
