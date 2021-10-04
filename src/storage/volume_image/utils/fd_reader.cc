// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/volume_image/utils/fd_reader.h"

#include <fcntl.h>
#include <lib/fpromise/result.h>
#include <lib/stdcompat/span.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>

#include <fbl/unique_fd.h>
#include <safemath/safe_conversions.h>

namespace storage::volume_image {

fpromise::result<FdReader, std::string> FdReader::Create(std::string_view path) {
  if (path.empty()) {
    return fpromise::error("Cannot obtain file descriptor from empty path.");
  }

  std::string pathname(path);
  fbl::unique_fd fd(open(pathname.c_str(), O_RDONLY));
  if (!fd.is_valid()) {
    std::string error = "Failed to obtain file descriptor from ";
    error.append(pathname).append(". More specifically ").append(strerror(errno));
    return fpromise::error(error);
  }
  struct stat file_stats = {};
  if (fstat(fd.get(), &file_stats) != 0) {
    return fpromise::error("Failed to obtain size for file descriptor at " + std::string(path) +
                           ". More specifically: " + strerror(errno));
  }

  return fpromise::ok(FdReader(std::move(fd), path, file_stats.st_size));
}

fpromise::result<void, std::string> FdReader::Read(uint64_t offset,
                                                   cpp20::span<uint8_t> buffer) const {
  size_t bytes_read = 0;
  while (bytes_read < buffer.size()) {
    uint8_t* destination = buffer.data() + bytes_read;
    size_t remaining_bytes = buffer.size() - bytes_read;
    off_t source_offset = offset + bytes_read;
    ssize_t result = pread(fd_.get(), destination, remaining_bytes, source_offset);

    if (result < 0) {
      std::string_view error_description(strerror(errno));
      std::string error = "Read failed from ";
      error.append(name_).append(". More specifically ").append(error_description);
      return fpromise::error(error);
    }
    if (result == 0) {
      std::string_view error_description(strerror(errno));
      std::string error = "Read failed from  ";
      error.append(name_).append(". End of file reached before reading requested bytes.");
      return fpromise::error(error);
    }

    bytes_read += result;
  }
  return fpromise::ok();
}

}  // namespace storage::volume_image
