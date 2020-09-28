// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/minfs/test/integration/utils.h"

#include <fcntl.h>
#include <sys/stat.h>

#include "src/storage/minfs/test/integration/minfs_fixtures.h"

std::string BuildPath(const std::string_view& name) {
  std::string path(kMountPath);
  if (name.compare(0, path.length(), path) == 0) {
    return std::string(name);
  }
  path.append(name);
  return path;
}

bool CreateDirectory(const std::string_view& name) {
  return mkdir(BuildPath(name).c_str(), 0755) == 0;
}

fbl::unique_fd CreateFile(const std::string_view& name) {
  return fbl::unique_fd(open(BuildPath(name).c_str(), O_RDWR | O_CREAT, 0644));
}

fbl::unique_fd OpenFile(const std::string_view& name, bool read_only) {
  int flags = read_only ? O_RDONLY : O_RDWR;
  return fbl::unique_fd(open(BuildPath(name).c_str(), flags, 0644));
}
