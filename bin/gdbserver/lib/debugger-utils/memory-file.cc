// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory-file.h"

#include <cinttypes>
#include <unistd.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "util.h"

namespace debugserver {
namespace util {

FileMemory::FileMemory(int fd)
    : fd_(fd) {
  FTL_DCHECK(fd >= 0);
}

FileMemory::~FileMemory() {
  close(fd_);
}

bool FileMemory::Read(uintptr_t address, void* out_buffer,
                      size_t length) const {
  FTL_DCHECK(out_buffer);

  off_t where = lseek(fd_, address, SEEK_SET);
  if (where != static_cast<off_t>(address)) {
    util::LogErrorWithErrno(ftl::StringPrintf("lseek to 0x%" PRIxPTR,
                                              address));
    return false;
  }

  ssize_t bytes_read = read(fd_, out_buffer, length);
  if (bytes_read < 0) {
    util::LogErrorWithErrno(
      ftl::StringPrintf("Failed to read memory at addr: 0x%" PRIxPTR,
                        address));
    return false;
  }

  if (length != static_cast<size_t>(bytes_read)) {
    FTL_LOG(ERROR) << ftl::StringPrintf("Short read, got %zu bytes, expected %zu",
                                        bytes_read, length);
    return false;
  }

  // TODO(dje): Dump the bytes read at sufficiently high logging level (>2).

  return true;
}

bool FileMemory::Write(uintptr_t address, const void* buffer,
                       size_t length) const {
  FTL_DCHECK(buffer);

  if (length == 0) {
    FTL_VLOG(2) << "No data to write";
    return true;
  }

  off_t where = lseek(fd_, address, SEEK_SET);
  if (where != static_cast<off_t>(address)) {
    util::LogErrorWithErrno(ftl::StringPrintf("lseek to 0x%" PRIxPTR,
                                              address));
    return false;
  }

  ssize_t bytes_written = write(fd_, buffer, length);
  if (bytes_written < 0) {
    util::LogErrorWithErrno(
      ftl::StringPrintf("Failed to read memory at addr: 0x%" PRIxPTR,
                        address));
    return false;
  }

  if (length != static_cast<size_t>(bytes_written)) {
    FTL_LOG(ERROR) << ftl::StringPrintf("Short write, wrote %zu bytes, expected %zu",
                                        bytes_written, length);
    return false;
  }

  // TODO(dje): Dump the bytes written at sufficiently high logging level (>2).

  return true;
}

}  // namespace util
}  // namespace debugserver
