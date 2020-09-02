// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FUZZING_FIDL_LIBFUZZER_H_
#define SRC_LIB_FUZZING_FIDL_LIBFUZZER_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>

// Defines the interface by LLVM exposed for fuzzing.
// See https://github.com/llvm/llvm-project/blob/master/compiler-rt/lib/fuzzer/FuzzerInterface.h

extern "C" {

// Required user functions

__EXPORT int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

// Optional user functions

__WEAK __EXPORT int LLVMFuzzerInitialize(int *argc, char ***argv);

// Other interfaces to libFuzzer

__EXPORT void LLVMFuzzerSetRemoteCallerPC(uintptr_t pc);

}  // extern "C"

// namespace fuzzer {

// } // namespace fuzzer

#endif  // SRC_LIB_FUZZING_FIDL_LIBFUZZER_H_
