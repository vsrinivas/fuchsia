// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/vmo/file.h"

#include <fcntl.h>
#include <magenta/syscalls.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "lib/ftl/logging.h"

namespace mtl {

bool VmoFromFd(ftl::UniqueFD fd, mx::vmo* handle_ptr) {
  FTL_CHECK(handle_ptr);

  struct stat64 st;
  if (fstat64(fd.get(), &st) == -1) {
    FTL_LOG(WARNING) << "mx::vmo::fstat failed";
    return false;
  }

  size_t size = st.st_size;
  mx_status_t status = mx::vmo::create(size, 0, handle_ptr);
  if (status != NO_ERROR) {
    FTL_LOG(WARNING) << "mx::vmo::create failed: " << status;
    return false;
  }

  constexpr size_t kBufferSize = 1 << 16;
  char buffer[kBufferSize];
  size_t offset = 0;
  while (offset < size) {
    ssize_t bytes_read = read(fd.get(), buffer, kBufferSize);
    if (bytes_read < 0) {
      FTL_LOG(WARNING) << "mx::vmo::read failed: " << bytes_read;
      return false;
    }

    mx_size_t actual = 0;
    mx_status_t rv = handle_ptr->write(buffer, offset, bytes_read, &actual);
    if (rv < 0 || actual != static_cast<mx_size_t>(bytes_read)) {
      FTL_LOG(WARNING) << "mx::vmo::write wrote " << actual
                       << " bytes instead of " << bytes_read << " bytes.";
      return false;
    }
    offset += bytes_read;
  }
  return true;
}

bool VmoFromFilename(const std::string& filename, mx::vmo* handle_ptr) {
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd == -1) {
    FTL_LOG(WARNING) << "mx::vmo::open failed to open file " << filename;
    return false;
  }
  return VmoFromFd(ftl::UniqueFD(fd), handle_ptr);
}

}  // namespace mtl
