// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_COMMON_HOST_UTIL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_COMMON_HOST_UTIL_H_

#include <string>

namespace zxdb {

// Returns the path (dir + file name) of the current executable.
std::string GetSelfPath();

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_COMMON_HOST_UTIL_H_
