// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

Options CopyOptions(const Options& options) {
  Options copy;
#define FUCHSIA_FUZZER_OPTION(type, option, Option, default_value) \
  if (options.has_##option()) {                                    \
    copy.set_##option(options.option());                           \
  }
#include "src/sys/fuzzing/common/options.inc"
#undef FUCHSIA_FUZZER_OPTION
  return copy;
}

}  // namespace fuzzing
