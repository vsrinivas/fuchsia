// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/fd_writer.h"

#include <fcntl.h>
#include <lib/fit/result.h>

#include <cstdio>
#include <cstdlib>

#include <fbl/span.h>
#include <fbl/unique_fd.h>

namespace storage::volume_image {

fit::result<FdWriter, std::string> FdWriter::Create(std::string_view path) {
  if (path.empty()) {
    return fit::error("Cannot obtain file descriptor from empty path.");
  }

  std::string pathname(path);
  fbl::unique_fd fd(open(pathname.c_str(), O_WRONLY));
  if (!fd.is_valid()) {
    return fit::error("Failed to obtain file descriptor from " + pathname +
                      ", More specifically: " + strerror(errno));
  }
  return fit::ok(FdWriter(std::move(fd), path));
}

std::string FdWriter::Write(uint64_t offset, fbl::Span<const uint8_t> buffer) {
  size_t bytes_written = 0;
  while (bytes_written < buffer.size()) {
    const uint8_t* source = buffer.data() + bytes_written;
    size_t remaining_bytes = buffer.size() - bytes_written;
    off_t target_offset = offset + bytes_written;
    int result = pwrite(fd_.get(), source, remaining_bytes, target_offset);

    if (result < 0) {
      return "Write failed from " + name_ + ". More specifically " + strerror(errno);
    }
    bytes_written += result;
  }
  return std::string();
}

}  // namespace storage::volume_image
