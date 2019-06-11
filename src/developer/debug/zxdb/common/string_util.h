// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_STRING_UTIL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_STRING_UTIL_H_

#include <string_view>

namespace zxdb {

// Returns true if the first argument begins in exactly the second.
bool StringBeginsWith(std::string_view str, std::string_view begins_with);

// Returns true if the first argument ends in exactly the second.
bool StringEndsWith(std::string_view str, std::string_view ends_with);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_STRING_UTIL_H_
