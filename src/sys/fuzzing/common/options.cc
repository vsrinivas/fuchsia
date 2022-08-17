// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

OptionsPtr MakeOptions() {
  auto options = std::make_shared<Options>();
  AddDefaults(options.get());
  return options;
}

Options CopyOptions(const Options& options) {
  Options copy;
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  if (options.has_##option()) {                                    \
    copy.set_##option(options.option());                           \
  }
#include "src/sys/fuzzing/common/options.inc"
#undef FUCHSIA_FUZZER_OPTION
  AddDefaults(&copy);
  return copy;
}

void SetOptions(Options* options, const Options& overrides) {
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  if (overrides.has_##option()) {                                  \
    options->set_##option(overrides.option());                     \
  }
#include "src/sys/fuzzing/common/options.inc"
#undef FUCHSIA_FUZZER_OPTION
}

void AddDefaults(Options* options) {
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  if (!options->has_##option()) {                                  \
    options->set_##option(default_value);                          \
  }
#include "src/sys/fuzzing/common/options.inc"
#undef FUCHSIA_FUZZER_OPTION
}

}  // namespace fuzzing
