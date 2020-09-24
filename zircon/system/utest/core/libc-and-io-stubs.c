// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <stdio.h>
#include <sys/uio.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/compiler.h>
#include <zircon/processargs.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/log.h>

// output via debuglog syscalls

static zx_handle_t log_handle;

#define LOGBUF_MAX (ZX_LOG_RECORD_MAX - sizeof(zx_log_record_t))

static mtx_t linebuffer_lock = MTX_INIT;
static char linebuffer[LOGBUF_MAX] __TA_GUARDED(linebuffer_lock);
static size_t linebuffer_size __TA_GUARDED(linebuffer_lock);

// Flushes and resets linebuffer.
static void flush_linebuffer_locked(void) __TA_REQUIRES(linebuffer_lock) {
  zx_debuglog_write(log_handle, 0, linebuffer, linebuffer_size);
  linebuffer_size = 0;
}

static void log_write(const void* data, size_t len) {
  mtx_lock(&linebuffer_lock);

  // printf calls write multiple times within a print, but each debuglog write
  // is a separate record, so each inserts a logical newline. To avoid
  // inappropriate breaking, do a version of _IOLBF here. A write of len == 0
  // indicates an fflush.

  if (len == 0) {
    flush_linebuffer_locked();
  }

  for (size_t i = 0; i < len; ++i) {
    if (linebuffer_size == sizeof(linebuffer)) {
      flush_linebuffer_locked();
    }
    char c = ((const char*)data)[i];
    linebuffer[linebuffer_size++] = c;
    if (c == '\n') {
      flush_linebuffer_locked();
    }
  }

  mtx_unlock(&linebuffer_lock);
}

// libc init and io stubs
// The reason these are here is that the "core" tests intentionally do not
// use fdio. See ./README.md.

static zx_handle_t root_resource;
static zx_handle_t mmio_root_resource;

__EXPORT
void __libc_extensions_init(uint32_t count, zx_handle_t handle[], uint32_t info[]) {
  for (unsigned n = 0; n < count; n++) {
    if (info[n] == PA_HND(PA_RESOURCE, 0)) {
      root_resource = handle[n];
      handle[n] = 0;
      info[n] = 0;
    }
    if (info[n] == PA_HND(PA_MMIO_RESOURCE, 0)) {
      mmio_root_resource = handle[n];
      handle[n] = 0;
      info[n] = 0;
    }
  }
  if (root_resource == ZX_HANDLE_INVALID) {
    static const char kStandaloneMsg[] =
        "*** Standalone core-tests must run directly from userboot ***\n";
    zx_debug_write(kStandaloneMsg, sizeof(kStandaloneMsg) - 1);
    __builtin_trap();
  } else {
    if (zx_debuglog_create(root_resource, 0, &log_handle) != ZX_OK) {
      zx_process_exit(-2);
    }
    static const char kStartMsg[] = "*** Running standalone Zircon core tests ***\n";
    zx_debuglog_write(log_handle, 0, kStartMsg, sizeof(kStartMsg) - 1);
  }
}

__EXPORT
zx_handle_t get_root_resource(void) { return root_resource; }

__EXPORT
zx_handle_t get_mmio_root_resource(void) { return mmio_root_resource; }

__EXPORT
ssize_t write(int fd, const void* data, size_t count) {
  if ((fd == 1) || (fd == 2)) {
    log_write(data, count);
  }
  return count;
}

__EXPORT
ssize_t readv(int fd, const struct iovec* iov, int num) { return 0; }

__EXPORT
ssize_t writev(int fd, const struct iovec* iov, int num) {
  ssize_t count = 0;
  ssize_t r;
  while (num > 0) {
    if (iov->iov_len != 0) {
      r = write(fd, iov->iov_base, iov->iov_len);
      if (r < 0) {
        return count ? count : r;
      }
      if ((size_t)r < iov->iov_len) {
        return count + r;
      }
      count += r;
    }
    iov++;
    num--;
  }
  return count;
}

__EXPORT
off_t lseek(int fd, off_t offset, int whence) {
  errno = ENOSYS;
  return -1;
}

__EXPORT
int isatty(int fd) { return 1; }
