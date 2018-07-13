// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "byte_block_file.h"

#include <unistd.h>
#include <cinttypes>

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "util.h"

namespace debugger_utils {

FileByteBlock::FileByteBlock(int fd) : fd_(fd) { FXL_DCHECK(fd >= 0); }

FileByteBlock::~FileByteBlock() { close(fd_); }

bool FileByteBlock::Read(uintptr_t address, void* out_buffer,
                         size_t length) const {
  FXL_DCHECK(out_buffer);

  off_t where = lseek(fd_, address, SEEK_SET);
  if (where != static_cast<off_t>(address)) {
    FXL_LOG(ERROR) << fxl::StringPrintf("lseek to 0x%" PRIxPTR, address) << ", "
                   << ErrnoString(errno);
    return false;
  }

  ssize_t bytes_read = read(fd_, out_buffer, length);
  if (bytes_read < 0) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
                          "Failed to read memory at addr: 0x%" PRIxPTR, address)
                   << ", " << ErrnoString(errno);
    return false;
  }

  if (length != static_cast<size_t>(bytes_read)) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "Short read, got %zu bytes, expected %zu", bytes_read, length);
    return false;
  }

  // TODO(dje): Dump the bytes read at sufficiently high logging level (>2).

  return true;
}

bool FileByteBlock::Write(uintptr_t address, const void* buffer,
                          size_t length) const {
  FXL_DCHECK(buffer);

  if (length == 0) {
    FXL_VLOG(2) << "No data to write";
    return true;
  }

  off_t where = lseek(fd_, address, SEEK_SET);
  if (where != static_cast<off_t>(address)) {
    FXL_LOG(ERROR) << fxl::StringPrintf("lseek to 0x%" PRIxPTR, address) << ", "
                   << ErrnoString(errno);
    return false;
  }

  ssize_t bytes_written = write(fd_, buffer, length);
  if (bytes_written < 0) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
                          "Failed to read memory at addr: 0x%" PRIxPTR, address)
                   << ", " << ErrnoString(errno);
    return false;
  }

  if (length != static_cast<size_t>(bytes_written)) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "Short write, wrote %zu bytes, expected %zu", bytes_written, length);
    return false;
  }

  // TODO(dje): Dump the bytes written at sufficiently high logging level (>2).

  return true;
}

}  // namespace debugger_utils
