// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <stdint.h>

#ifdef __Fuchsia__
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>
#include <stddef.h>
#endif

__BEGIN_CDECLS

#if !defined(__x86_64__)
#error "unsupported architecture"
#endif

#define IPT_MSR_BITS(len, shift) (((1ULL << (len)) - 1) << (shift))

// Bits in the IA32_RTIT_CTL MSR.
// These bits are writable by the user with ioctl_insntrace_set_ctl_config.
// The driver will override a setting if it's unsafe (e.g., causes #gpf).

#define IPT_CTL_CYC_EN_SHIFT (1)
#define IPT_CTL_CYC_EN_LEN (1)
#define IPT_CTL_CYC_EN_MASK IPT_MSR_BITS(IPT_CTL_CYC_EN_LEN, IPT_CTL_CYC_EN_SHIFT)

#define IPT_CTL_OS_ALLOWED_SHIFT (2)
#define IPT_CTL_OS_ALLOWED_LEN (1)
#define IPT_CTL_OS_ALLOWED_MASK IPT_MSR_BITS(IPT_CTL_OS_ALLOWED_LEN, IPT_CTL_OS_ALLOWED_SHIFT)

#define IPT_CTL_USER_ALLOWED_SHIFT (3)
#define IPT_CTL_USER_ALLOWED_LEN (1)
#define IPT_CTL_USER_ALLOWED_MASK IPT_MSR_BITS(IPT_CTL_USER_ALLOWED_LEN, IPT_CTL_USER_ALLOWED_SHIFT)

#define IPT_CTL_POWER_EVENT_EN_SHIFT (4)
#define IPT_CTL_POWER_EVENT_EN_LEN (1)
#define IPT_CTL_POWER_EVENT_EN_MASK \
  IPT_MSR_BITS(IPT_CTL_POWER_EVENT_EN_LEN, IPT_CTL_POWER_EVENT_EN_SHIFT)

#define IPT_CTL_FUP_ON_PTW_SHIFT (5)
#define IPT_CTL_FUP_ON_PTW_LEN (1)
#define IPT_CTL_FUP_ON_PTW_MASK IPT_MSR_BITS(IPT_CTL_FUP_ON_PTW_LEN, IPT_CTL_FUP_ON_PTW_SHIFT)

#define IPT_CTL_CR3_FILTER_SHIFT (7)
#define IPT_CTL_CR3_FILTER_LEN (1)
#define IPT_CTL_CR3_FILTER_MASK IPT_MSR_BITS(IPT_CTL_CR3_FILTER_LEN, IPT_CTL_CR3_FILTER_SHIFT)

#define IPT_CTL_MTC_EN_SHIFT (9)
#define IPT_CTL_MTC_EN_LEN (1)
#define IPT_CTL_MTC_EN_MASK IPT_MSR_BITS(IPT_CTL_MTC_EN_LEN, IPT_CTL_MTC_EN_SHIFT)

#define IPT_CTL_TSC_EN_SHIFT (10)
#define IPT_CTL_TSC_EN_LEN (1)
#define IPT_CTL_TSC_EN_MASK IPT_MSR_BITS(IPT_CTL_TSC_EN_LEN, IPT_CTL_TSC_EN_SHIFT)

#define IPT_CTL_DIS_RETC_SHIFT (11)
#define IPT_CTL_DIS_RETC_LEN (1)
#define IPT_CTL_DIS_RETC_MASK IPT_MSR_BITS(IPT_CTL_DIS_RETC_LEN, IPT_CTL_DIS_RETC_SHIFT)

#define IPT_CTL_PTW_EN_SHIFT (12)
#define IPT_CTL_PTW_EN_LEN (1)
#define IPT_CTL_PTW_EN_MASK IPT_MSR_BITS(IPT_CTL_PTW_EN_LEN, IPT_CTL_PTW_EN_SHIFT)

#define IPT_CTL_BRANCH_EN_SHIFT (13)
#define IPT_CTL_BRANCH_EN_LEN (1)
#define IPT_CTL_BRANCH_EN_MASK IPT_MSR_BITS(IPT_CTL_BRANCH_EN_LEN, IPT_CTL_BRANCH_EN_SHIFT)

#define IPT_CTL_MTC_FREQ_SHIFT (14)
#define IPT_CTL_MTC_FREQ_LEN (4)
#define IPT_CTL_MTC_FREQ_MASK IPT_MSR_BITS(IPT_CTL_MTC_FREQ_LEN, IPT_CTL_MTC_FREQ_SHIFT)

#define IPT_CTL_CYC_THRESH_SHIFT (19)
#define IPT_CTL_CYC_THRESH_LEN (4)
#define IPT_CTL_CYC_THRESH_MASK IPT_MSR_BITS(IPT_CTL_CYC_THRESH_LEN, IPT_CTL_CYC_THRESH_SHIFT)

#define IPT_CTL_PSB_FREQ_SHIFT (24)
#define IPT_CTL_PSB_FREQ_LEN (4)
#define IPT_CTL_PSB_FREQ_MASK IPT_MSR_BITS(IPT_CTL_PSB_FREQ_LEN, IPT_CTL_PSB_FREQ_SHIFT)

#define IPT_CTL_ADDR0_SHIFT (32)
#define IPT_CTL_ADDR_LEN (4)
#define IPT_CTL_ADDR0_MASK IPT_MSR_BITS(IPT_CTL_ADDR_LEN, IPT_CTL_ADDR0_SHIFT)

#define IPT_CTL_ADDR1_SHIFT (36)
#define IPT_CTL_ADDR1_MASK IPT_MSR_BITS(IPT_CTL_ADDR_LEN, IPT_CTL_ADDR1_SHIFT)

#define IPT_CTL_ADDR2_SHIFT (40)
#define IPT_CTL_ADDR2_MASK IPT_MSR_BITS(IPT_CTL_ADDR_LEN, IPT_CTL_ADDR2_SHIFT)

#define IPT_CTL_ADDR3_SHIFT (44)
#define IPT_CTL_ADDR3_MASK IPT_MSR_BITS(IPT_CTL_ADDR_LEN, IPT_CTL_ADDR3_SHIFT)

// Other bits in IA32_RTIT_CTL MSR, not writable with ioctl_insntrace_set_ctl_config.

#define IPT_CTL_TRACE_EN_SHIFT (0)
#define IPT_CTL_TRACE_EN_LEN (1)
#define IPT_CTL_TRACE_EN_MASK IPT_MSR_BITS(IPT_CTL_TRACE_EN_LEN, IPT_CTL_TRACE_EN_SHIFT)

#define IPT_CTL_FABRIC_EN_SHIFT (6)
#define IPT_CTL_FABRIC_EN_LEN (1)
#define IPT_CTL_FABRIC_EN_MASK IPT_MSR_BITS(IPT_CTL_FABRIC_EN_LEN, IPT_CTL_FABRIC_EN_SHIFT)

#define IPT_CTL_TOPA_SHIFT (8)
#define IPT_CTL_TOPA_LEN (1)
#define IPT_CTL_TOPA_MASK IPT_MSR_BITS(IPT_CTL_TOPA_LEN, IPT_CTL_TOPA_SHIFT)

// Masks for reading IA32_RTIT_STATUS.

#define IPT_STATUS_FILTER_EN_SHIFT (0)
#define IPT_STATUS_FILTER_EN_LEN (1)
#define IPT_STATUS_FILTER_EN_MASK IPT_MSR_BITS(IPT_STATUS_FILTER_EN_LEN, IPT_STATUS_FILTER_EN_SHIFT)

#define IPT_STATUS_CONTEXT_EN_SHIFT (1)
#define IPT_STATUS_CONTEXT_EN_LEN (1)
#define IPT_STATUS_CONTEXT_EN_MASK \
  IPT_MSR_BITS(IPT_STATUS_CONTEXT_EN_LEN, IPT_STATUS_CONTEXT_EN_SHIFT)

#define IPT_STATUS_TRIGGER_EN_SHIFT (2)
#define IPT_STATUS_TRIGGER_EN_LEN (1)
#define IPT_STATUS_TRIGGER_EN_MASK \
  IPT_MSR_BITS(IPT_STATUS_TRIGGER_EN_LEN, IPT_STATUS_TRIGGER_EN_SHIFT)

#define IPT_STATUS_ERROR_SHIFT (4)
#define IPT_STATUS_ERROR_LEN (1)
#define IPT_STATUS_ERROR_MASK IPT_MSR_BITS(IPT_STATUS_ERROR_LEN, IPT_STATUS_ERROR_SHIFT)

#define IPT_STATUS_STOPPED_SHIFT (5)
#define IPT_STATUS_STOPPED_LEN (1)
#define IPT_STATUS_STOPPED_MASK IPT_MSR_BITS(IPT_STATUS_STOPPED_LEN, IPT_STATUS_STOPPED_SHIFT)

#define IPT_STATUS_PACKET_BYTE_COUNT_SHIFT (32)
#define IPT_STATUS_PACKET_BYTE_COUNT_LEN (17)
#define IPT_STATUS_PACKET_BYTE_COUNT_MASK                       \
  IPT_MSR_BITS(IPT_STATUS_IPT_STATUS_PACKET_BYTE_COUNT_LEN_LEN, \
               IPT_STATUS_IPT_STATUS_PACKET_BYTE_COUNT_LEN_SHIFT)

// Maximum number of address ranges that h/w may support.
// No chip supports more than 2 at the moment.
// Plus the XSAVES docs don't define more than 2 (see Intel Vol 3 section
// 36.3.5.2 "Trace Configuration Context Switch using XSAVES/XRSTORS").
#define IPT_MAX_NUM_ADDR_RANGES 2

// Valid ToPA entry sizes.
#define IPT_TOPA_MIN_SHIFT 12
#define IPT_TOPA_MAX_SHIFT 27

// These bits are for internal use

// Macros for building entries for the Table of Physical Addresses
#define IPT_TOPA_ENTRY_PHYS_ADDR(x) ((uint64_t)(x) & ~((1ULL << 12) - 1))
#define IPT_TOPA_ENTRY_SIZE(size_log2) ((uint64_t)((size_log2)-12) << 6)
#define IPT_TOPA_ENTRY_STOP (1ULL << 4)
#define IPT_TOPA_ENTRY_INT (1ULL << 2)
#define IPT_TOPA_ENTRY_END (1ULL << 0)

// Macros for extracting info from ToPA entries
#define IPT_TOPA_ENTRY_EXTRACT_PHYS_ADDR(e) ((zx_paddr_t)((e) & ~((1ULL << 12) - 1)))
#define IPT_TOPA_ENTRY_EXTRACT_SIZE(e) ((uint)((((e) >> 6) & 0xf) + 12))

// Arbitarily picked constants for ourselves.
// ToPA tables are 16KB in size (technically can be up to 256MB).
// A 16KB table provides 2047 non-END entries, so at the
// minimum can provide a capture buffer of just under 8MB.
#define IPT_TOPA_MAX_TABLE_ENTRIES 2048

// The maximum value for |zx_insntrace_trace_config_t.num_traces|.
#define IPT_MAX_NUM_TRACES 64

// Two "modes" of tracing are supported:
typedef enum {
  // trace each cpu, regardless of what's running on it
  IPT_MODE_CPU,
  // trace specific threads
  IPT_MODE_THREAD
} zx_insntrace_trace_mode_t;

// A trace configuration.
// This is used to pass data from the driver to the kernel.
typedef struct {
  // One of IPT_MODE_{CPUS,THREADS}.
  uint32_t mode;
  // The number of traces to create.
  // In CPU mode this must be zx_system_get_num_cpus().
  // In THREAD mode this is the maximum number of threads for which traces
  // will be collected. Buffer space is allocated on demand, but the
  // underlying data structure has a maximum. The value can be at most
  // IPT_MAX_NUM_TRACES.
  uint32_t num_traces;
} zx_insntrace_trace_config_t;

// An integer identifying a particular buffer.
typedef uint32_t zx_insntrace_buffer_descriptor_t;

// The PT register set.
// This is used to pass data from the driver to the kernel.
// This is accessed via mtrace, but basically it's a regset.
typedef struct {
  uint64_t ctl;
  uint64_t status;
  uint64_t output_base;
  uint64_t output_mask_ptrs;
  uint64_t cr3_match;
  struct {
    uint64_t a, b;
  } addr_ranges[IPT_MAX_NUM_ADDR_RANGES];
} zx_x86_pt_regs_t;

__END_CDECLS
