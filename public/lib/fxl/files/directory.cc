// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/files/directory.h"

#include <limits.h>
#include <sys/stat.h>
#include <vector>

#include "lib/fxl/files/path.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/portable_unistd.h"

namespace files {

std::string GetCurrentDirectory() {
  char buffer[PATH_MAX];
  FXL_CHECK(getcwd(buffer, sizeof(buffer)));
  return std::string(buffer);
}

bool IsDirectory(const std::string& path) {
  struct stat buf;
  if (stat(path.c_str(), &buf) != 0)
    return false;
  return S_ISDIR(buf.st_mode);
}

bool CreateDirectory(const std::string& full_path) {
  std::vector<std::string> subpaths;

  // Collect a list of all parent directories.
  std::string last_path = full_path;
  subpaths.push_back(full_path);
  for (std::string path = GetDirectoryName(full_path);
       !path.empty() && path != last_path; path = GetDirectoryName(path)) {
    subpaths.push_back(path);
    last_path = path;
  }

  // Iterate through the parents and create the missing ones.
  for (auto it = subpaths.rbegin(); it != subpaths.rend(); ++it) {
    if (IsDirectory(*it))
      continue;
    if (mkdir(it->c_str(), 0700) == 0)
      continue;
    // Mkdir failed, but it might be due to the directory appearing out of thin
    // air. This can occur if two processes are trying to create the same file
    // system tree at the same time. Check to see if it exists and make sure it
    // is a directory.
    if (!IsDirectory(*it))
      return false;
  }
  return true;
}

}  // namespace files
