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

// Define defaults.
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  constexpr type kDefault##Option = default_value;
#include "src/sys/fuzzing/common/options.inc"
#undef FUCHSIA_FUZZER_OPTION

// Provides the ability to copy Options, as the FIDL-generated struct implicitly deletes the
// copy-constructor.
Options CopyOptions(const Options& options);

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_OPTIONS_H_
