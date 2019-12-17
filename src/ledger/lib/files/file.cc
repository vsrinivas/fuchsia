// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/files/file.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define FILE_CREATE_MODE 0666

#include "src/ledger/lib/files/eintr_wrapper.h"
#include "src/ledger/lib/files/file_descriptor.h"
#include "src/ledger/lib/files/unique_fd.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {
namespace {

template <typename T>
bool ReadFileDescriptor(int fd, T* result) {
  LEDGER_DCHECK(result);
  result->clear();

  if (fd < 0)
    return false;

  constexpr size_t kBufferSize = 1 << 16;
  size_t offset = 0;
  ssize_t bytes_read = 0;
  do {
    offset += bytes_read;
    result->resize(offset + kBufferSize);
    bytes_read = HANDLE_EINTR(read(fd, &(*result)[offset], kBufferSize));
  } while (bytes_read > 0);

  if (bytes_read < 0) {
    result->clear();
    return false;
  }

  result->resize(offset + bytes_read);
  return true;
}

}  // namespace

bool WriteFileAt(int dirfd, const std::string& path, const char* data, ssize_t size) {
  unique_fd fd(
      HANDLE_EINTR(openat(dirfd, path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, FILE_CREATE_MODE)));
  if (!fd.is_valid())
    return false;
  return WriteFileDescriptor(fd.get(), data, size);
}

bool ReadFileToStringAt(int dirfd, const std::string& path, std::string* result) {
  unique_fd fd(openat(dirfd, path.c_str(), O_RDONLY));
  return ReadFileDescriptor(fd.get(), result);
}
bool IsFileAt(int dirfd, const std::string& path) {
  struct stat stat_buffer;
  if (fstatat(dirfd, path.c_str(), &stat_buffer, /* flags = */ 0) != 0)
    return false;
  return S_ISREG(stat_buffer.st_mode);
}

bool GetFileSizeAt(int dirfd, const std::string& path, uint64_t* size) {
  struct stat stat_buffer;
  if (fstatat(dirfd, path.c_str(), &stat_buffer, /* flags = */ 0) != 0)
    return false;
  *size = stat_buffer.st_size;
  return true;
}

}  // namespace ledger
