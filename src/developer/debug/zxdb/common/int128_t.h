// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_INT128_T_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_INT128_T_H_

#include <string>

namespace zxdb {

using int128_t = __int128;
using uint128_t = unsigned __int128;

// std::to_string version for this type (the standard library doesn't define it).
std::string to_string(int128_t i);
std::string to_string(uint128_t i);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_INT128_T_H_
