// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains routines to consume |fuchsia.fuzzer.CoverageData| FIDL structures.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_COVERAGE_DATA_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_COVERAGE_DATA_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/zx/process.h>
#include <stdint.h>

#include <string>

#include "src/sys/fuzzing/framework/target/process.h"

namespace fuzzing {

// Returns the target identifier for the given |process|.
uint64_t GetTargetId(const zx::process& process);

// Returns target identifier encoded in the name of the |inline_8bit_counters| VMO, or
// |kInvalidTargetId| if no identifier could be parsed.
uint64_t GetTargetId(const zx::vmo& inline_8bit_counters);

// Returns the target identifier encoded in the given |id|, or |kInvalidTargetId| if no
// identifier could be parsed.
uint64_t GetTargetId(const std::string& id);

// Returns the module identifier encoded in the name of the |inline_8bit_counters| VMO, or an empty
// string if no identifier could be parsed.
std::string GetModuleId(const zx::vmo& inline_8bit_counters);

// Returns the module identifier encoded in the given |id|, or an empty string if no
// identifier could be parsed.
std::string GetModuleId(const std::string& id);

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_COVERAGE_DATA_H_
