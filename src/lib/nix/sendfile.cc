// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/sendfile.h>

#include <stdint.h>
#include <unistd.h>

#include <algorithm>

ssize_t sendfile(int out_fd, int in_fd, off_t* offset, size_t count) {
  uint8_t buffer[32 * 1024];
  size_t written = 0;
  while (written < count) {
    ssize_t result = 0;
    size_t capacity = std::min(sizeof(buffer), count - written);
    if (offset != nullptr) {
      result = pread(in_fd, buffer, capacity, *offset);
    } else {
      result = read(in_fd, buffer, capacity);
    }
    if (result < 0) {
      return -1;
    }
    if (result == 0) {
      break;
    }
    size_t buffer_offset = 0u;
    size_t buffer_count = static_cast<size_t>(result);
    if (offset) {
      *offset += buffer_count;
    }
    while (buffer_offset < buffer_count)  {
      result = write(out_fd, buffer + buffer_offset, buffer_count - buffer_offset);
      if (result < 0) {
        return -1;
      }
      buffer_offset += result;
    }
    written += buffer_count;
  }
  return written;
}
