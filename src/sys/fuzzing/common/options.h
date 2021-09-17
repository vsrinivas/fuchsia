// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_OPTIONS_H_
#define SRC_SYS_FUZZING_COMMON_OPTIONS_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/zx/time.h>
#include <stdint.h>

#include <memory>

namespace fuzzing {

using ::fuchsia::fuzzer::Options;

// Keep these in sync with the documentation in the FIDL file.
// TODO(fxbug.dev/84367): Some customers have also requested a "min_input_size" option.
constexpr uint32_t kDefaultRuns = 0;
constexpr zx::duration kDefaultMaxTotalTime = zx::sec(0);
constexpr uint32_t kDefaultSeed = 0;
constexpr uint64_t kDefaultMaxInputSize = 1ULL << 20;
constexpr uint16_t kDefaultMutationDepth = 5;
constexpr uint16_t kDefaultDictionaryLevel = 0;
constexpr bool kDefaultDetectExits = false;
constexpr bool kDefaultDetectLeaks = false;
constexpr zx::duration kDefaultRunLimit = zx::sec(1200);
constexpr uint64_t kDefaultMallocLimit = 2ULL << 30;
constexpr uint64_t kDefaultOOMLimit = 2ULL << 30;
constexpr zx::duration kDefaultPurgeInterval = zx::sec(1);
constexpr int32_t kDefaultMallocExitCode = 2000;
constexpr int32_t kDefaultDeathExitCode = 2001;
constexpr int32_t kDefaultLeakExitCode = 2002;
constexpr int32_t kDefaultOOMExitCode = 2003;
constexpr zx::duration kDefaultPulseInterval = zx::sec(20);

// Populates any missing fields in the given |options| table with the default values above.
void AddDefaults(Options* options);

// Returns a set of default options.
std::shared_ptr<Options> DefaultOptions();

// Provides the ability to copy Options, as the FIDL-generated struct implicitly deletes the
// copy-constructor.
Options CopyOptions(const Options& options);

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_OPTIONS_H_
