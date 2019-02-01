// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/far/file_operations.h"

#include <fcntl.h>

#include "garnet/lib/far/alignment.h"
#include "lib/fxl/files/unique_fd.h"

namespace archive {

bool CopyPathToFile(const char* src_path, int dst_fd, uint64_t length) {
  fxl::UniqueFD src_fd(open(src_path, O_RDONLY));
  if (!src_fd.is_valid()) {
    FXL_LOG(INFO) << "Failed to open " << src_path;
    return false;
  }
  return CopyFileToFile(src_fd.get(), dst_fd, length);
}

bool CopyFileToPath(int src_fd, const char* dst_path, uint64_t length) {
  fxl::UniqueFD dst_fd(open(dst_path, O_WRONLY | O_CREAT | O_TRUNC,
                            S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH));
  if (!dst_fd.is_valid())
    return false;
  return CopyFileToFile(src_fd, dst_fd.get(), length);
}

bool CopyFileToFile(int src_fd, int dst_fd, uint64_t length) {
  constexpr uint64_t kBufferSize = 64 * 1024;
  char buffer[kBufferSize];
  ssize_t actual = 0;
  for (uint64_t copied = 0; copied < length; copied += actual) {
    uint64_t requested =
        std::min(kBufferSize, static_cast<uint64_t>(length - copied));
    actual = read(src_fd, buffer, requested);
    if (actual <= 0)
      return false;
    if (!fxl::WriteFileDescriptor(dst_fd, buffer, actual))
      return false;
  }
  return true;
}

}  // namespace archive
