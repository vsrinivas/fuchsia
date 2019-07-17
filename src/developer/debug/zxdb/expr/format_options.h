// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_OPTIONS_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_OPTIONS_H_

#include <stdint.h>

namespace zxdb {

struct FormatOptions {
  enum class NumFormat { kDefault, kUnsigned, kSigned, kHex, kChar };

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

  bool enable_pretty_printing = true;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_FORMAT_OPTIONS_H_
