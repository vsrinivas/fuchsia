// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Note on naming: The "m" in "mtrace" means "miscellaneous". "trace" is being
// used very generically, e.g., all the different kinds of h/w based trace
// and performance data capturing one can do.

#pragma once

__BEGIN_CDECLS

// mtrace_control() can operate on a range of features.
// It's an abstraction that doesn't mean much, and will likely be replaced
// before it's useful; it's here in the interests of hackability in the
// interim.
#define MTRACE_KIND_INSNTRACE 0
#define MTRACE_KIND_PERFMON 1

// Actions for instruction tracing control

// Allocate in-kernel resources needed for the trace.
#define MTRACE_INSNTRACE_ALLOC_TRACE 0

// Free everything allocated with MTRACE_INSNTRACE_ALLOC_TRACE.
#define MTRACE_INSNTRACE_FREE_TRACE 1

// Stage all trace buffer data for the specified buffer descriptor.
#define MTRACE_INSNTRACE_STAGE_TRACE_DATA 2

// Fetch trace buffer data (MSRs) for the specified buffer descriptor.
#define MTRACE_INSNTRACE_GET_TRACE_DATA 3

#define MTRACE_INSNTRACE_START 4
#define MTRACE_INSNTRACE_STOP 5

// Actions for CPU Performance Counters/Statistics control

// Get performonce monitoring system properties
// The result is an mx_x86_ipm_properties_t struct filled in.
#define MTRACE_PERFMON_GET_PROPERTIES 0

// Prepare the kernel for performance data collection trace runs.
#define MTRACE_PERFMON_INIT 1

// Assign a buffer to the specified cpu.
#define MTRACE_PERFMON_ASSIGN_BUFFER 2

// Stage the perf config for a CPU.
// Will allocate resources as necessary.
// Must be called with data collection off.
#define MTRACE_PERFMON_STAGE_CONFIG 3

// Start data collection.
// Must be called after STAGE_CONFIG with data collection off.
#define MTRACE_PERFMON_START 4

// Stop data collection.
// May be called before START.
// May be called multiple times.
#define MTRACE_PERFMON_STOP 5

// Finish data collection.
// Must be called with data collection off.
// Must be called when done: frees various resources allocated to perform
// the data collection.
// May be called multiple times.
#define MTRACE_PERFMON_FINI 6

// Encode/decode options values for mtrace_control().
// At present we just encode the cpu number here.
// We only support 32 cpus at the moment, the extra bit is for magic values.
#define MTRACE_PERFMON_OPTIONS_CPU_MASK 0x3f
#define MTRACE_PERFMON_OPTIONS(cpu) ((cpu)&MTRACE_PERFMON_OPTIONS_CPU_MASK)

#define MTRACE_PERFMON_ALL_CPUS 32

#define MTRACE_PERFMON_OPTIONS_CPU(options) ((options)&MTRACE_PERFMON_OPTIONS_CPU_MASK)

__END_CDECLS
