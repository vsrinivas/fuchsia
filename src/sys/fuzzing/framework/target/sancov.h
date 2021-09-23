// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_TARGET_SANCOV_H_
#define SRC_SYS_FUZZING_FRAMEWORK_TARGET_SANCOV_H_

// The following are symbols that SanitizerCoverage expects the runtime to provide.

extern "C" {

// See https://clang.llvm.org/docs/SanitizerCoverage.html#inline-8bit-counters
// NOLINTNEXTLINE(bugprone-reserved-identifier)
void __sanitizer_cov_8bit_counters_init(uint8_t* start, uint8_t* stop);

// See https://clang.llvm.org/docs/SanitizerCoverage.html#pc-table
// NOLINTNEXTLINE(bugprone-reserved-identifier)
void __sanitizer_cov_pcs_init(const uintptr_t* start, const uintptr_t* stop);

}  // extern "C"

#endif  // SRC_SYS_FUZZING_FRAMEWORK_TARGET_SANCOV_H_
