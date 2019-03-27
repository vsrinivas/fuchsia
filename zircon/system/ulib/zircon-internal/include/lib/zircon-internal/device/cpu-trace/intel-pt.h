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

#define IPT_API_VERSION 2

#define IPT_MSR_BITS(len, shift) (((1ULL << (len)) - 1) << (shift))

// Bits in the IA32_RTIT_CTL MSR.
// These bits are writable by the user with ioctl_insntrace_set_ctl_config.
// The driver will override a setting if it's unsafe (e.g., causes #gpf).

#define IPT_CTL_CYC_EN_SHIFT (1)
#define IPT_CTL_CYC_EN_LEN   (1)
#define IPT_CTL_CYC_EN_MASK  IPT_MSR_BITS(IPT_CTL_CYC_EN_LEN, IPT_CTL_CYC_EN_SHIFT)

#define IPT_CTL_OS_ALLOWED_SHIFT (2)
#define IPT_CTL_OS_ALLOWED_LEN   (1)
#define IPT_CTL_OS_ALLOWED_MASK  IPT_MSR_BITS(IPT_CTL_OS_ALLOWED_LEN, IPT_CTL_OS_ALLOWED_SHIFT)

#define IPT_CTL_USER_ALLOWED_SHIFT (3)
#define IPT_CTL_USER_ALLOWED_LEN   (1)
#define IPT_CTL_USER_ALLOWED_MASK  IPT_MSR_BITS(IPT_CTL_USER_ALLOWED_LEN, IPT_CTL_USER_ALLOWED_SHIFT)

#define IPT_CTL_POWER_EVENT_EN_SHIFT (4)
#define IPT_CTL_POWER_EVENT_EN_LEN   (1)
#define IPT_CTL_POWER_EVENT_EN_MASK  IPT_MSR_BITS(IPT_CTL_POWER_EVENT_EN_LEN, IPT_CTL_POWER_EVENT_EN_SHIFT)

#define IPT_CTL_FUP_ON_PTW_SHIFT (5)
#define IPT_CTL_FUP_ON_PTW_LEN   (1)
#define IPT_CTL_FUP_ON_PTW_MASK  IPT_MSR_BITS(IPT_CTL_FUP_ON_PTW_LEN, IPT_CTL_FUP_ON_PTW_SHIFT)

#define IPT_CTL_CR3_FILTER_SHIFT (7)
#define IPT_CTL_CR3_FILTER_LEN   (1)
#define IPT_CTL_CR3_FILTER_MASK  IPT_MSR_BITS(IPT_CTL_CR3_FILTER_LEN, IPT_CTL_CR3_FILTER_SHIFT)

#define IPT_CTL_MTC_EN_SHIFT (9)
#define IPT_CTL_MTC_EN_LEN   (1)
#define IPT_CTL_MTC_EN_MASK  IPT_MSR_BITS(IPT_CTL_MTC_EN_LEN, IPT_CTL_MTC_EN_SHIFT)

#define IPT_CTL_TSC_EN_SHIFT (10)
#define IPT_CTL_TSC_EN_LEN   (1)
#define IPT_CTL_TSC_EN_MASK  IPT_MSR_BITS(IPT_CTL_TSC_EN_LEN, IPT_CTL_TSC_EN_SHIFT)

#define IPT_CTL_DIS_RETC_SHIFT (11)
#define IPT_CTL_DIS_RETC_LEN   (1)
#define IPT_CTL_DIS_RETC_MASK  IPT_MSR_BITS(IPT_CTL_DIS_RETC_LEN, IPT_CTL_DIS_RETC_SHIFT)

#define IPT_CTL_PTW_EN_SHIFT (12)
#define IPT_CTL_PTW_EN_LEN   (1)
#define IPT_CTL_PTW_EN_MASK  IPT_MSR_BITS(IPT_CTL_PTW_EN_LEN, IPT_CTL_PTW_EN_SHIFT)

#define IPT_CTL_BRANCH_EN_SHIFT (13)
#define IPT_CTL_BRANCH_EN_LEN   (1)
#define IPT_CTL_BRANCH_EN_MASK  IPT_MSR_BITS(IPT_CTL_BRANCH_EN_LEN, IPT_CTL_BRANCH_EN_SHIFT)

#define IPT_CTL_MTC_FREQ_SHIFT (14)
#define IPT_CTL_MTC_FREQ_LEN   (4)
#define IPT_CTL_MTC_FREQ_MASK  IPT_MSR_BITS(IPT_CTL_MTC_FREQ_LEN, IPT_CTL_MTC_FREQ_SHIFT)

#define IPT_CTL_CYC_THRESH_SHIFT (19)
#define IPT_CTL_CYC_THRESH_LEN   (4)
#define IPT_CTL_CYC_THRESH_MASK  IPT_MSR_BITS(IPT_CTL_CYC_THRESH_LEN, IPT_CTL_CYC_THRESH_SHIFT)

#define IPT_CTL_PSB_FREQ_SHIFT (24)
#define IPT_CTL_PSB_FREQ_LEN   (4)
#define IPT_CTL_PSB_FREQ_MASK  IPT_MSR_BITS(IPT_CTL_PSB_FREQ_LEN, IPT_CTL_PSB_FREQ_SHIFT)

#define IPT_CTL_ADDR0_SHIFT (32)
#define IPT_CTL_ADDR_LEN    (4)
#define IPT_CTL_ADDR0_MASK  IPT_MSR_BITS(IPT_CTL_ADDR_LEN, IPT_CTL_ADDR0_SHIFT)

#define IPT_CTL_ADDR1_SHIFT (36)
#define IPT_CTL_ADDR1_MASK  IPT_MSR_BITS(IPT_CTL_ADDR_LEN, IPT_CTL_ADDR1_SHIFT)

#define IPT_CTL_ADDR2_SHIFT (40)
#define IPT_CTL_ADDR2_MASK  IPT_MSR_BITS(IPT_CTL_ADDR_LEN, IPT_CTL_ADDR2_SHIFT)

#define IPT_CTL_ADDR3_SHIFT (44)
#define IPT_CTL_ADDR3_MASK  IPT_MSR_BITS(IPT_CTL_ADDR_LEN, IPT_CTL_ADDR3_SHIFT)

// Other bits in IA32_RTIT_CTL MSR, not writable with ioctl_insntrace_set_ctl_config.

#define IPT_CTL_TRACE_EN_SHIFT (0)
#define IPT_CTL_TRACE_EN_LEN   (1)
#define IPT_CTL_TRACE_EN_MASK  IPT_MSR_BITS(IPT_CTL_TRACE_EN_LEN, IPT_CTL_TRACE_EN_SHIFT)

#define IPT_CTL_FABRIC_EN_SHIFT (6)
#define IPT_CTL_FABRIC_EN_LEN   (1)
#define IPT_CTL_FABRIC_EN_MASK  IPT_MSR_BITS(IPT_CTL_FABRIC_EN_LEN, IPT_CTL_FABRIC_EN_SHIFT)

#define IPT_CTL_TOPA_SHIFT (8)
#define IPT_CTL_TOPA_LEN   (1)
#define IPT_CTL_TOPA_MASK  IPT_MSR_BITS(IPT_CTL_TOPA_LEN, IPT_CTL_TOPA_SHIFT)

// Masks for reading IA32_RTIT_STATUS.

#define IPT_STATUS_FILTER_EN_SHIFT (0)
#define IPT_STATUS_FILTER_EN_LEN   (1)
#define IPT_STATUS_FILTER_EN_MASK  IPT_MSR_BITS(IPT_STATUS_FILTER_EN_LEN, IPT_STATUS_FILTER_EN_SHIFT)

#define IPT_STATUS_CONTEXT_EN_SHIFT (1)
#define IPT_STATUS_CONTEXT_EN_LEN   (1)
#define IPT_STATUS_CONTEXT_EN_MASK  IPT_MSR_BITS(IPT_STATUS_CONTEXT_EN_LEN, IPT_STATUS_CONTEXT_EN_SHIFT)

#define IPT_STATUS_TRIGGER_EN_SHIFT (2)
#define IPT_STATUS_TRIGGER_EN_LEN   (1)
#define IPT_STATUS_TRIGGER_EN_MASK  IPT_MSR_BITS(IPT_STATUS_TRIGGER_EN_LEN, IPT_STATUS_TRIGGER_EN_SHIFT)

#define IPT_STATUS_ERROR_SHIFT (4)
#define IPT_STATUS_ERROR_LEN   (1)
#define IPT_STATUS_ERROR_MASK  IPT_MSR_BITS(IPT_STATUS_ERROR_LEN, IPT_STATUS_ERROR_SHIFT)

#define IPT_STATUS_STOPPED_SHIFT (5)
#define IPT_STATUS_STOPPED_LEN   (1)
#define IPT_STATUS_STOPPED_MASK  IPT_MSR_BITS(IPT_STATUS_STOPPED_LEN, IPT_STATUS_STOPPED_SHIFT)

#define IPT_STATUS_PACKET_BYTE_COUNT_SHIFT (32)
#define IPT_STATUS_PACKET_BYTE_COUNT_LEN   (17)
#define IPT_STATUS_PACKET_BYTE_COUNT_MASK  \
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
#define IPT_TOPA_ENTRY_PHYS_ADDR(x) ((uint64_t)(x) & ~((1ULL<<12)-1))
#define IPT_TOPA_ENTRY_SIZE(size_log2) ((uint64_t)((size_log2) - 12) << 6)
#define IPT_TOPA_ENTRY_STOP (1ULL << 4)
#define IPT_TOPA_ENTRY_INT (1ULL << 2)
#define IPT_TOPA_ENTRY_END (1ULL << 0)

// Macros for extracting info from ToPA entries
#define IPT_TOPA_ENTRY_EXTRACT_PHYS_ADDR(e) \
  ((zx_paddr_t)((e) & ~((1ULL<<12)-1)))
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
} zx_x86_pt_regs_t;

// Two "modes" of tracing are supported:
// trace each cpu, regardless of what's running on it
#define IPT_MODE_CPUS 0
// trace specific threads
#define IPT_MODE_THREADS 1

///////////////////////////////////////////////////////////////////////////////

#ifdef __Fuchsia__

// ioctls

typedef uint32_t zx_itrace_buffer_descriptor_t;

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
} ioctl_insntrace_trace_config_t;

// The maximum value for |ioctl_ipt_trace_config_t.num_traces|.
#define IPT_MAX_NUM_TRACES 64

// must be called prior to START, allocate buffers of the specified size
// Input: ioctl_insntrace_trace_config_t
// TODO(dje): Later return a trace descriptor.
#define IOCTL_INSNTRACE_ALLOC_TRACE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INSNTRACE, 0)
IOCTL_WRAPPER_IN(ioctl_insntrace_alloc_trace, IOCTL_INSNTRACE_ALLOC_TRACE, ioctl_insntrace_trace_config_t)

// release resources allocated with IOCTL_INSNTRACE_ALLOC_TRACE
// TODO(dje): Later allow allocating multiple traces.
#define IOCTL_INSNTRACE_FREE_TRACE \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INSNTRACE, 1)
IOCTL_WRAPPER(ioctl_insntrace_free_trace, IOCTL_INSNTRACE_FREE_TRACE)

// fetch the config of a trace
// Output: ioctl_insntrace_trace_config_t
#define IOCTL_INSNTRACE_GET_TRACE_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INSNTRACE, 2)
IOCTL_WRAPPER_OUT(ioctl_insntrace_get_trace_config, IOCTL_INSNTRACE_GET_TRACE_CONFIG, ioctl_insntrace_trace_config_t)

typedef struct {
    // A "buffer" is made up of |num_chunks| chunks with each chunk having size
    // |1<<chunk_order| pages.
    uint32_t num_chunks;
    // #pages as a power of 2
    uint32_t chunk_order;
    bool is_circular;
    uint64_t ctl;
    uint64_t cr3_match;
    struct {
        uint64_t a,b;
    } addr_ranges[IPT_MAX_NUM_ADDR_RANGES];
} ioctl_insntrace_buffer_config_t;

// allocate a trace buffer
// Input: ioctl_insntrace_buffer_config_t
// Output: trace buffer descriptor (think file descriptor for trace buffers)
// When tracing cpus, buffers are auto-assigned to cpus: the resulting trace
// buffer descriptor is the number of the cpu using the buffer.
#define IOCTL_INSNTRACE_ALLOC_BUFFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INSNTRACE, 3)
IOCTL_WRAPPER_INOUT(ioctl_insntrace_alloc_buffer, IOCTL_INSNTRACE_ALLOC_BUFFER, ioctl_insntrace_buffer_config_t, zx_itrace_buffer_descriptor_t)

typedef struct {
    // for IOCTL_KIND_SET_HANDLE first element must be the handle
    zx_handle_t thread;
    zx_itrace_buffer_descriptor_t descriptor;
} ioctl_insntrace_assign_thread_buffer_t;

// assign a buffer to a thread
// Input: ioctl_insntrace_assign_thread_buffer_t
#define IOCTL_INSNTRACE_ASSIGN_THREAD_BUFFER \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_INSNTRACE, 4)
IOCTL_WRAPPER_IN(ioctl_insntrace_assign_thread_buffer, IOCTL_INSNTRACE_ASSIGN_THREAD_BUFFER,
                 ioctl_insntrace_assign_thread_buffer_t)

// release a buffer from a thread
// Input: ioctl_insntrace_assign_thread_buffer_t
#define IOCTL_INSNTRACE_RELEASE_THREAD_BUFFER \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_INSNTRACE, 5)
IOCTL_WRAPPER_IN(ioctl_insntrace_release_thread_buffer, IOCTL_INSNTRACE_RELEASE_THREAD_BUFFER,
                 ioctl_insntrace_assign_thread_buffer_t)

// return config data for a trace buffer
// Input: zx_itrace_buffer_descriptor_t
// Output: ioctl_insntrace_trace_buffer_config_t
#define IOCTL_INSNTRACE_GET_BUFFER_CONFIG \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INSNTRACE, 6)
IOCTL_WRAPPER_INOUT(ioctl_insntrace_get_buffer_config, IOCTL_INSNTRACE_GET_BUFFER_CONFIG,
                    zx_itrace_buffer_descriptor_t, ioctl_insntrace_buffer_config_t)

// This contains the run-time produced data about the buffer.
// Not the trace data itself, just info about the data.
typedef struct {
    // N.B. This is the offset in the buffer where tracing stopped (treating
    // all buffers as one large one). If using a circular buffer then all of
    // the buffer may contain data, there's no current way to know if tracing
    // wrapped without scanning records.
    uint64_t capture_end;
} ioctl_insntrace_buffer_info_t;

// get trace data associated with the buffer
// Input: zx_itrace_buffer_descriptor_t
// Output: ioctl_insntrace_buffer_info_t
#define IOCTL_INSNTRACE_GET_BUFFER_INFO \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INSNTRACE, 7)
IOCTL_WRAPPER_INOUT(ioctl_insntrace_get_buffer_info, IOCTL_INSNTRACE_GET_BUFFER_INFO,
                    zx_itrace_buffer_descriptor_t, ioctl_insntrace_buffer_info_t)

typedef struct {
    zx_itrace_buffer_descriptor_t descriptor;
    uint32_t chunk_num;
} ioctl_insntrace_chunk_handle_req_t;

// return a handle of a chunk of the trace buffer
// There is no API to get N handles, we have to get them one at a time.
// [There's no point in trying to micro-optimize this and, say, get 3 at
// a time.]
#define IOCTL_INSNTRACE_GET_CHUNK_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_INSNTRACE, 8)
IOCTL_WRAPPER_INOUT(ioctl_insntrace_get_chunk_handle, IOCTL_INSNTRACE_GET_CHUNK_HANDLE,
                    ioctl_insntrace_chunk_handle_req_t, zx_handle_t)

// free a trace buffer
// Input: zx_itrace_buffer_descriptor_t
#define IOCTL_INSNTRACE_FREE_BUFFER \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INSNTRACE, 9)
IOCTL_WRAPPER_IN(ioctl_insntrace_free_buffer, IOCTL_INSNTRACE_FREE_BUFFER,
                 zx_itrace_buffer_descriptor_t)

// turn on processor tracing
#define IOCTL_INSNTRACE_START \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INSNTRACE, 10)
IOCTL_WRAPPER(ioctl_insntrace_start, IOCTL_INSNTRACE_START)

// turn off processor tracing
#define IOCTL_INSNTRACE_STOP \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_INSNTRACE, 11)
IOCTL_WRAPPER(ioctl_insntrace_stop, IOCTL_INSNTRACE_STOP)

#endif // __Fuchsia__

__END_CDECLS
