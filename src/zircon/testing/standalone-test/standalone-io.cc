// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/standalone-test/standalone.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/resource.h>
#include <stdio.h>
#include <sys/uio.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <mutex>
#include <string_view>

// output via debuglog syscalls

namespace standalone {

// printf calls write multiple times within a print, but each debuglog write
// is a separate record, so each inserts a logical newline. To avoid
// inappropriate breaking, do a version of _IOLBF here. A write of len == 0
// indicates an fflush.
void LogWrite(std::string_view str) {
  static zx::debuglog log = []() {
    zx::debuglog log;
    zx_status_t status = zx::debuglog::create(*standalone::GetRootResource(), 0, &log);
    if (status != ZX_OK) {
      zx_process_exit(status);
    }
    return log;
  }();

  static std::mutex linebuffer_lock;
  static char linebuffer[ZX_LOG_RECORD_DATA_MAX] __TA_GUARDED(linebuffer_lock);
  static size_t linebuffer_size __TA_GUARDED(linebuffer_lock);
  std::lock_guard lock(linebuffer_lock);

  constexpr auto flush = []() __TA_REQUIRES(linebuffer_lock) {
    log.write(0, linebuffer, linebuffer_size);
    linebuffer_size = 0;
  };

  if (str.empty()) {
    flush();
    return;
  }

  for (char c : str) {
    if (linebuffer_size == sizeof(linebuffer)) {
      flush();
    }
    linebuffer[linebuffer_size++] = c;
    if (c == '\n') {
      flush();
    }
  }
}

}  // namespace standalone

// These replace libc functions that ordinarily would be supplied by fdio.
// When standalone::LogWrite is called, since these are defined in the same
// file they will be linked in to override the weak definitions in libc.

extern "C" {

__EXPORT ssize_t write(int fd, const void* data, size_t count) {
  if ((fd == 1) || (fd == 2)) {
    standalone::LogWrite({reinterpret_cast<const char*>(data), count});
  }
  return static_cast<ssize_t>(count);
}

__EXPORT ssize_t readv(int fd, const struct iovec* iov, int num) { return 0; }

__EXPORT ssize_t writev(int fd, const struct iovec* iov, int num) {
  ssize_t count = 0;
  ssize_t r;
  while (num > 0) {
    if (iov->iov_len != 0) {
      r = write(fd, iov->iov_base, iov->iov_len);
      if (r < 0) {
        return count ? count : r;
      }
      if (static_cast<size_t>(r) < iov->iov_len) {
        return count + r;
      }
      count += r;
    }
    iov++;
    num--;
  }
  return count;
}

__EXPORT off_t lseek(int fd, off_t offset, int whence) {
  errno = ENOSYS;
  return -1;
}

__EXPORT int isatty(int fd) { return 1; }

}  // extern "C"
