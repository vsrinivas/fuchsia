// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/build_config.h"
#include "lib/ftl/files/path.h"
#include "lib/ftl/files/symlink.h"

#include <limits.h>
#include <unistd.h>

#include "lib/ftl/logging.h"

namespace files {

bool ReadSymbolicLink(const std::string& path, std::string* resolved_path) {
  FTL_DCHECK(!path.empty());
  FTL_DCHECK(resolved_path);

  char buffer[PATH_MAX];
  ssize_t length = readlink(path.c_str(), buffer, sizeof(buffer));

  if (length <= 0) {
    resolved_path->clear();
    return false;
  }

  *resolved_path = std::string(buffer, length);
  return true;
}

std::string GetAbsoluteFilePath(const std::string& path) {
#if defined(OS_FUCHSIA)
  // realpath() isn't supported by Fuchsia. See MG-425.
  return SimplifyPath(AbsolutePath(path));
#else
  char buffer[PATH_MAX];
  if (realpath(path.c_str(), buffer) == nullptr)
    return std::string();
  return buffer;
#endif  // defined(OS_FUCHSIA)
}

}  // namespace files
