// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

////////////////////////////////////////////////////////////////////////////////////////////////////
// This file generates the sanitizer_common interface.
//
// To generate a header file, include it with SANITIZER_COV_HEADER_ONLY #defined.
//
// To generate a source file, include it with SANITIZER_COV_PROXY #defined to be a class with static
// methods like |fuzzing::SanitizerCovProxy|, and GET_CALLER_PC to produce values like the return
// address of the current function.
////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdint.h>
#include <zircon/compiler.h>

#include "traced-instruction.h"

#ifdef SANITIZER_COV_HEADER_ONLY

#define SANITIZER_COV_INTERFACE(sancov_suffix, sancov_args, proxy_method, proxy_args) \
  __EXPORT extern "C" void __sanitizer_cov_##sancov_suffix sancov_args;

#else  // SANITIZER_COV_HEADER_ONLY

#define SANITIZER_COV_INTERFACE(sancov_suffix, sancov_args, proxy_method, proxy_args) \
  __EXPORT extern "C" void __sanitizer_cov_##sancov_suffix sancov_args {              \
    ::fuzzing::SANITIZER_COV_PROXY::proxy_method proxy_args;                          \
  }

#endif  // SANITIZER_COV_HEADER_ONLY

SANITIZER_COV_INTERFACE(8bit_counters_init, (uint8_t * start, uint8_t *stop), Init8BitCounters,
                        (start, stop))

SANITIZER_COV_INTERFACE(pcs_init, (const uintptr_t *pcs_beg, const uintptr_t *pcs_end), InitPcs,
                        (pcs_beg, pcs_end))

SANITIZER_COV_INTERFACE(trace_pc_indir, (uintptr_t callee), Trace,
                        (::fuzzing::Instruction::kPcIndir, GET_CALLER_PC(), callee, 0))

SANITIZER_COV_INTERFACE(trace_cmp8, (uint64_t arg1, uint64_t arg2), Trace,
                        (::fuzzing::Instruction::kCmp8, GET_CALLER_PC(), arg1, arg2))

SANITIZER_COV_INTERFACE(trace_const_cmp8, (uint64_t arg1, uint64_t arg2), Trace,
                        (::fuzzing::Instruction::kConstCmp8, GET_CALLER_PC(), arg1, arg2))

SANITIZER_COV_INTERFACE(trace_cmp4, (uint32_t arg1, uint32_t arg2), Trace,
                        (::fuzzing::Instruction::kCmp4, GET_CALLER_PC(), arg1, arg2))

SANITIZER_COV_INTERFACE(trace_const_cmp4, (uint32_t arg1, uint32_t arg2), Trace,
                        (::fuzzing::Instruction::kConstCmp4, GET_CALLER_PC(), arg1, arg2))

SANITIZER_COV_INTERFACE(trace_cmp2, (uint16_t arg1, uint16_t arg2), Trace,
                        (::fuzzing::Instruction::kCmp2, GET_CALLER_PC(), arg1, arg2))

SANITIZER_COV_INTERFACE(trace_const_cmp2, (uint16_t arg1, uint16_t arg2), Trace,
                        (::fuzzing::Instruction::kConstCmp2, GET_CALLER_PC(), arg1, arg2))

SANITIZER_COV_INTERFACE(trace_cmp1, (uint8_t arg1, uint8_t arg2), Trace,
                        (::fuzzing::Instruction::kCmp1, GET_CALLER_PC(), arg1, arg2))

SANITIZER_COV_INTERFACE(trace_const_cmp1, (uint8_t arg1, uint8_t arg2), Trace,
                        (::fuzzing::Instruction::kConstCmp1, GET_CALLER_PC(), arg1, arg2))

SANITIZER_COV_INTERFACE(trace_switch, (uint64_t val, uint64_t *cases), TraceSwitch,
                        (GET_CALLER_PC(), val, cases))

SANITIZER_COV_INTERFACE(trace_div4, (uint32_t val), Trace,
                        (::fuzzing::Instruction::kDiv4, GET_CALLER_PC(), val, 0))

SANITIZER_COV_INTERFACE(trace_div8, (uint64_t val), Trace,
                        (::fuzzing::Instruction::kDiv8, GET_CALLER_PC(), val, 0))

SANITIZER_COV_INTERFACE(trace_gep, (uintptr_t idx), Trace,
                        (::fuzzing::Instruction::kGep, GET_CALLER_PC(), idx, 0))

#undef SANITIZER_COV_INTERFACE
