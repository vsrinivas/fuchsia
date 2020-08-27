// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_TRACED_INSTRUCTION_H_
#define SRC_LIB_FUZZING_FIDL_TRACED_INSTRUCTION_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

namespace fuzzing {

// Describes a traced instruction, including what instruction it is, where it was called, and up
// to two parameters.
struct Instruction {
  enum Type : uint64_t {
    kInvalid = 0,
    kPcIndir,
    kCmp8,
    kConstCmp8,
    kCmp4,
    kConstCmp4,
    kCmp2,
    kConstCmp2,
    kCmp1,
    kConstCmp1,
    kDiv8,
    kDiv4,
    kGep,
    kSentinel,
    kMaxValue = kSentinel,
  } type;
  uintptr_t pc;
  uint64_t args[2];
};

// This is the agreed upon size between the Coverage and Instrumented objects of how much buffer to
// reserve for traces per process. Chosen to be 1 MB of memory to avoid overly-frequent updates.
const size_t kInstructionBufferLen = 16384;
const size_t kNumInstructionBuffers = 2;
const size_t kMaxInstructions = kInstructionBufferLen * kNumInstructionBuffers;

// Shared VMO signals to/from the fuzzing engine.
const zx_signals_t kShutdown = ZX_USER_SIGNAL_0;
const zx_signals_t kReadableSignalA = ZX_USER_SIGNAL_1;
const zx_signals_t kWritableSignalA = ZX_USER_SIGNAL_2;
const zx_signals_t kReadableSignalB = ZX_USER_SIGNAL_3;
const zx_signals_t kWritableSignalB = ZX_USER_SIGNAL_4;
const zx_signals_t kInIteration = ZX_USER_SIGNAL_5;
const zx_signals_t kBetweenIterations = ZX_USER_SIGNAL_6;

}  // namespace fuzzing

#endif  // SRC_LIB_FUZZING_FIDL_TRACED_INSTRUCTION_H_
