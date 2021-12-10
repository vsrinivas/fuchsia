// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/zx/debuglog.h>
#include <lib/zx/resource.h>
#include <stdio.h>
#include <sys/uio.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

#include <string_view>

#include "standalone.h"

// output via debuglog syscalls

namespace {

constexpr std::string_view kStartMsg = "*** Running standalone Zircon core tests ***\n";

zx::debuglog gLogHandle;

mtx_t linebuffer_lock = MTX_INIT;
char linebuffer[ZX_LOG_RECORD_DATA_MAX] __TA_GUARDED(linebuffer_lock);
size_t linebuffer_size __TA_GUARDED(linebuffer_lock);

// Flushes and resets linebuffer.
void flush_linebuffer_locked() __TA_REQUIRES(linebuffer_lock) {
  gLogHandle.write(0, linebuffer, linebuffer_size);
  linebuffer_size = 0;
}

void LogWrite(std::string_view str) {
  mtx_lock(&linebuffer_lock);

  // printf calls write multiple times within a print, but each debuglog write
  // is a separate record, so each inserts a logical newline. To avoid
  // inappropriate breaking, do a version of _IOLBF here. A write of len == 0
  // indicates an fflush.

  if (str.empty()) {
    flush_linebuffer_locked();
  }

  for (char c : str) {
    if (linebuffer_size == sizeof(linebuffer)) {
      flush_linebuffer_locked();
    }
    linebuffer[linebuffer_size++] = c;
    if (c == '\n') {
      flush_linebuffer_locked();
    }
  }

  mtx_unlock(&linebuffer_lock);
}

}  // namespace

void StandaloneInitIo(zx_handle_t root_resource) {
  zx_status_t status = zx::debuglog::create(*zx::unowned_resource{root_resource}, 0, &gLogHandle);
  if (status != ZX_OK) {
    zx_process_exit(-2);
  }
  LogWrite(kStartMsg);
}

extern "C" {

__EXPORT ssize_t write(int fd, const void* data, size_t count) {
  if ((fd == 1) || (fd == 2)) {
    LogWrite({reinterpret_cast<const char*>(data), count});
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
