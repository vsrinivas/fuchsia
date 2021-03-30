// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "path.h"

#include <sys/stat.h>

namespace fs_management {

std::string GetBinaryPath(const char* file) {
  std::string path = std::string("/boot/bin/") + file;
  struct stat stat_buf;
  if (stat(path.c_str(), &stat_buf) == -1 && errno == ENOENT) {
    return std::string("/pkg/bin/") + file;
  } else {
    return path;
  }
}

}  // namespace fs_management
