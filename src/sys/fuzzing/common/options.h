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
using ::fuchsia::fuzzer::SanitizerOptions;

// Define defaults.
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  constexpr type kDefault##Option = default_value;
#include "src/sys/fuzzing/common/options-literal.inc"
#undef FUCHSIA_FUZZER_OPTION

// Aliases to simplify passing around the shared options.
using OptionsPtr = std::shared_ptr<Options>;
OptionsPtr MakeOptions();

// Provides the ability to copy Options, as the FIDL-generated struct implicitly deletes the
// copy-constructor.
Options CopyOptions(const Options& options);

// Applies any set values in |overrides| to the given set of |options|.
void SetOptions(Options* options, const Options& overrides);

// Sets any missing options to their default values.
void AddDefaults(Options* options);

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_OPTIONS_H_
