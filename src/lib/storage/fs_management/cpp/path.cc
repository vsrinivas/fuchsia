// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/fs_management/cpp/path.h"

#include <sys/stat.h>

namespace fs_management {

std::string GetBinaryPath(const char* file) {
  std::string path = std::string("/pkg/bin/") + file;
  struct stat stat_buf;
  if (stat(path.c_str(), &stat_buf) == -1 && errno == ENOENT) {
    return std::string("/boot/bin/") + file;
  } else {
    return path;
  }
}

}  // namespace fs_management
