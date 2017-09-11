// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_STRINGS_SPLIT_STRING_H_
#define LIB_FXL_STRINGS_SPLIT_STRING_H_

#include <string>
#include <vector>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/strings/string_view.h"

namespace fxl {

enum WhiteSpaceHandling {
  kKeepWhitespace,
  kTrimWhitespace,
};

enum SplitResult {
  // Strictly return all results.
  kSplitWantAll,

  // Only nonempty results will be added to the results.
  kSplitWantNonEmpty,
};

// Split the given string on ANY of the given separators, returning copies of
// the result
FXL_EXPORT std::vector<std::string> SplitStringCopy(
    StringView input,
    StringView separators,
    WhiteSpaceHandling whitespace,
    SplitResult result_type);

// Like SplitStringCopy above except it returns a vector of StringViews which
// reference the original buffer without copying.
FXL_EXPORT std::vector<StringView> SplitString(StringView input,
                                               StringView separators,
                                               WhiteSpaceHandling whitespace,
                                               SplitResult result_type);

}  // namespace fxl

#endif  // LIB_FXL_STRINGS_SPLIT_STRING_H_
