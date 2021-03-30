// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_PATH_H_
#define SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_PATH_H_

#include <string>

namespace fs_management {

// Returns the binary path for the given file, which will either be with a /boot/bin prefix or a
// /pkg/bin prefix (for test environments).
std::string GetBinaryPath(const char* file);

}  // namespace fs_management

#endif  // SRC_LIB_STORAGE_FS_MANAGEMENT_CPP_PATH_H_
