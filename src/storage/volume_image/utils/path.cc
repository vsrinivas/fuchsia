// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/path.h"

#include <limits.h>
#include <unistd.h>

#include <filesystem>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace storage::volume_image {

std::string GetBasePath() {
  std::string result;
#if defined(__APPLE__)
  // Executable path can have relative references ("..") depending on how the
  // app was launched.
  uint32_t length = 0;
  _NSGetExecutablePath(nullptr, &length);
  result.resize(length);
  _NSGetExecutablePath(result.data(), &length);
  result.resize(length - 1);  // Length included terminator.
#elif defined(__linux__)
  // The realpath() call below will resolve the symbolic link.
  result.assign("/proc/self/exe");
#else
  result = "./";
#endif

  char fullpath[PATH_MAX];
  std::filesystem::path path = std::string(realpath(result.c_str(), fullpath));
  return path.remove_filename().c_str();
}

}  // namespace storage::volume_image
