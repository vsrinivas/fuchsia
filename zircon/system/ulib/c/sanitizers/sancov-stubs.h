// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// These macros call SANCOV_STUB(NAME) for each __sanitizer_cov_NAME symbol
// that represents a function called by instrumented code.
//
// SANCOV_STUBS covers all the entry points.
// SANCOV_NOOP_STUBS covers only the subset of SANCOV_STUBS where each
// entry point is ordinarily a no-op that might be called harmlessly
// by code during early startup before the proper runtime is in place.

#define SANCOV_STUBS                 \
    SANCOV_STUB(trace_pc_guard)      \
    SANCOV_STUB(trace_pc_guard_init) \
    SANCOV_NOOP_STUBS

#define SANCOV_NOOP_STUBS            \
    SANCOV_STUB(trace_cmp)           \
    SANCOV_STUB(trace_cmp1)          \
    SANCOV_STUB(trace_cmp2)          \
    SANCOV_STUB(trace_cmp4)          \
    SANCOV_STUB(trace_cmp8)          \
    SANCOV_STUB(trace_const_cmp1)    \
    SANCOV_STUB(trace_const_cmp2)    \
    SANCOV_STUB(trace_const_cmp4)    \
    SANCOV_STUB(trace_const_cmp8)    \
    SANCOV_STUB(trace_switch)        \
    SANCOV_STUB(trace_div4)          \
    SANCOV_STUB(trace_div8)          \
    SANCOV_STUB(trace_gep)           \
    SANCOV_STUB(trace_pc)            \
    SANCOV_STUB(trace_pc_indir)      \
    SANCOV_STUB(8bit_counters_init)  \
    SANCOV_STUB(pcs_init)
