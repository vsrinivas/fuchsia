// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/files/file.h"

#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

#include "lib/ftl/files/eintr_wrapper.h"
#include "lib/ftl/files/file_descriptor.h"
#include "lib/ftl/files/unique_fd.h"
#include "lib/ftl/logging.h"

namespace files {

bool WriteFile(const std::string& path, const char* data, ssize_t size) {
  ftl::UniqueFD fd(HANDLE_EINTR(creat(path.c_str(), 0666)));
  if (!fd.is_valid())
    return false;
  return ftl::WriteFileDescriptor(fd.get(), data, size);
}

bool ReadFileToString(const std::string& path, std::string* result) {
  FTL_DCHECK(result);
  result->clear();

  ftl::UniqueFD fd(open(path.c_str(), O_RDONLY));
  if (!fd.is_valid())
    return false;

  constexpr size_t kBufferSize = 1 << 16;
  size_t offset = 0;
  ssize_t bytes_read = 0;
  do {
    offset += bytes_read;
    result->resize(offset + kBufferSize);
    bytes_read = HANDLE_EINTR(read(fd.get(), &(*result)[offset], kBufferSize));
  } while (bytes_read > 0);

  if (bytes_read < 0) {
    result->clear();
    return false;
  }

  result->resize(offset + bytes_read);
  return true;
}

}  // namespace files
