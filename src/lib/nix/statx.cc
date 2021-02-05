// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <lib/fdio/fdio.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/statx.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>

#include "private.h"

int statx(int dirfd, const char *pathname, int flags, unsigned int mask, struct statx *buf) {
  struct stat s;

  if (buf == nullptr || pathname == nullptr) {
    return ERRNO(EFAULT);
  }

  if (pathname[0] == '\0') {
    return ERRNO(EINVAL);
  }

  int ret = fstatat(dirfd, pathname, &s, 0);
  if (ret != 0) {
    return ret;
  }

  // Fill up the necessary fields for statx from stat.
  memset(buf, 0, sizeof(struct statx));

  if (mask & STATX_MODE) {
    buf->stx_mode = s.st_mode;
    buf->stx_mask |= STATX_MODE;
  }

  if (mask & STATX_INO) {
    buf->stx_ino = s.st_ino;
    buf->stx_mask |= STATX_INO;
  }

  if (mask & STATX_SIZE) {
    buf->stx_size = s.st_size;
    buf->stx_mask |= STATX_SIZE;
  }

  buf->stx_blksize = s.st_blksize;

  if (mask & STATX_BLOCKS) {
    // stx_blocks are blocks of size 512.
    buf->stx_blocks = s.st_blocks * VNATTR_BLKSIZE / 512;
    buf->stx_mask |= STATX_BLOCKS;
  }

  if (mask & STATX_NLINK) {
    buf->stx_nlink = s.st_nlink;
    buf->stx_mask |= STATX_NLINK;
  }

  if (mask & STATX_BTIME) {
    buf->stx_btime.tv_sec = s.st_ctim.tv_sec;
    buf->stx_btime.tv_nsec = s.st_ctim.tv_nsec;
    buf->stx_mask |= STATX_BTIME;
  }

  if (mask & STATX_MTIME) {
    buf->stx_mtime.tv_sec = s.st_mtim.tv_sec;
    buf->stx_mtime.tv_nsec = s.st_mtim.tv_nsec;
    buf->stx_mask |= STATX_MTIME;
  }

  return 0;
}
