// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// N.B. This is ideally temporary. It is used by Intel PT support, and is a
// stopgap until "resources" can be used to read/write x86 MSRs.
// "mtrace" == "zircon trace": the idea being to be a generalization of
// ktrace. It's all temporary, but there may be other uses before the stopgap
// is no longer necessary.

#pragma once

__BEGIN_CDECLS

// mtrace_control() can operate on a range of features, for now just IPT.
// It's an abstraction that doesn't mean much, and will likely be replaced
// before it's useful; it's here in the interests of hackability in the
// interim.
#define MTRACE_KIND_IPT 0

// Actions for perf_control

// Allocate in-kernel resources needed for the trace.
#define MTRACE_IPT_ALLOC_TRACE 0

// Free everything allocated with MTRACE_IPT_ALLOC_TRACE.
#define MTRACE_IPT_FREE_TRACE 1

// Stage all trace buffer data for a CPU.
#define MTRACE_IPT_STAGE_CPU_DATA 2

// Fetch trace buffer data (MSRs) for a CPU.
#define MTRACE_IPT_GET_CPU_DATA 3

#define MTRACE_IPT_CPU_MODE_START 4
#define MTRACE_IPT_CPU_MODE_STOP 5

// Encode/decode options values for mtrace_control().
// At present we just encode the cpu number here.
// We only support 32 cpus at the moment, the extra bit is for magic values.
#define MTRACE_IPT_OPTIONS_CPU_MASK 0x3f
#define MTRACE_IPT_OPTIONS(cpu) (((cpu) & MTRACE_IPT_OPTIONS_CPU_MASK) + 0)

#define MTRACE_IPT_ALL_CPUS 32

#define MTRACE_IPT_OPTIONS_CPU(options) ((options) & MTRACE_IPT_OPTIONS_CPU_MASK)

__END_CDECLS
