// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/options.h"

#include <lib/syslog/cpp/macros.h>

namespace fuzzing {
namespace {

const char* kSanOptions = "SAN_OPTIONS";

void SetOptionsImpl(Options* options, const Options& overrides) {
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  if (overrides.has_##option()) {                                  \
    options->set_##option(overrides.option());                     \
  }
#include "src/sys/fuzzing/common/options.inc"
#undef FUCHSIA_FUZZER_OPTION
}

}  // namespace

OptionsPtr MakeOptions() {
  auto options = std::make_shared<Options>();
  AddDefaults(options.get());
  return options;
}

Options CopyOptions(const Options& options) {
  Options copy;
  SetOptions(&copy, options);
  AddDefaults(&copy);
  return copy;
}

void SetOptions(Options* options, const Options& overrides) {
  // First, create a mutable copy that can be validated.
  Options valid;
  SetOptionsImpl(&valid, overrides);

  // Next, make any changes needed to make `valid` valid.
  if (valid.has_sanitizer_options()) {
    // Per options.fidl, sanitizer_options with an invalid name are ignored.
    auto name = valid.sanitizer_options().name;
    auto suffix_len = strlen(kSanOptions);
    if (name.size() < suffix_len ||
        name.compare(name.size() - suffix_len, suffix_len, kSanOptions)) {
      if (!name.empty()) {
        FX_LOGS(WARNING) << "Ignoring invalid sanitizer_options: '" << name << "'";
      }
      valid.clear_sanitizer_options();
    }
  }

  // Finally, apply the `valid` options to the returned `options`.
  SetOptionsImpl(options, valid);
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
