// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

namespace zxdb {

struct FormatExprValueOptions {
  enum class NumFormat { kDefault, kUnsigned, kSigned, kHex, kChar };

  // This has numeric values so one can compare verbosity levels.
  enum class Verbosity : int {
    // Show as little as possible without being misleading. Some long types
    // will be elided with "...", references won't have addresses.
    kMinimal = 0,

    // Print like GDB does. Show the full names of base classes, reference
    // addresses, and pointer types.
    kMedium = 1,

    // All full type information and pointer values are shown for everything.
    kAllTypes = 2
  };

  // Maximum number of elements to print in an array. For strings we'll
  // speculatively fetch this much data since we don't know mow long the string
  // will be in advance. This means that increasing this will make all string
  // printing (even small strings) slower.
  //
  // If we want to support larger sizes, we may want to add a special memory
  // request option where the debug agent fetches until a null terminator is
  // reached.
  uint32_t max_array_size = 256;

  // Format to apply to numeric types.
  NumFormat num_format = NumFormat::kDefault;

  Verbosity verbosity = Verbosity::kMedium;
};

}  // namespace zxdb
