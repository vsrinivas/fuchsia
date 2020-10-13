// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fvm-host/file-wrapper.h"

#include <memory>

namespace fvm::host {

ssize_t FdWrapper::Read(void* buffer, size_t count) { return read(fd_, buffer, count); }

ssize_t FdWrapper::Write(const void* buffer, size_t count) { return write(fd_, buffer, count); }

ssize_t FdWrapper::Seek(off_t offset, int whence) { return lseek(fd_, offset, whence); }

ssize_t FdWrapper::Size() {
  off_t curr = lseek(fd_, 0, SEEK_CUR);
  off_t size = lseek(fd_, 0, SEEK_END);
  lseek(fd_, curr, SEEK_SET);
  return size;
}

ssize_t FdWrapper::Tell() { return lseek(fd_, 0, SEEK_CUR); }

zx_status_t FdWrapper::Truncate(size_t size) { return ftruncate(fd_, size); }

zx_status_t FdWrapper::Sync() { return fsync(fd_); }

zx_status_t UniqueFdWrapper::Open(const char* path, int flags, mode_t mode,
                                  std::unique_ptr<UniqueFdWrapper>* out) {
  fbl::unique_fd fd(open(path, flags, mode));
  if (!fd) {
    return ZX_ERR_IO;
  }

  *out = std::make_unique<UniqueFdWrapper>(std::move(fd));
  return ZX_OK;
}

ssize_t UniqueFdWrapper::Read(void* buffer, size_t count) { return read(fd_.get(), buffer, count); }

ssize_t UniqueFdWrapper::Write(const void* buffer, size_t count) {
  return write(fd_.get(), buffer, count);
}

ssize_t UniqueFdWrapper::Seek(off_t offset, int whence) { return lseek(fd_.get(), offset, whence); }

ssize_t UniqueFdWrapper::Size() {
  off_t curr = lseek(fd_.get(), 0, SEEK_CUR);
  off_t size = lseek(fd_.get(), 0, SEEK_END);
  lseek(fd_.get(), curr, SEEK_SET);
  return size;
}

ssize_t UniqueFdWrapper::Tell() { return lseek(fd_.get(), 0, SEEK_CUR); }

zx_status_t UniqueFdWrapper::Truncate(size_t size) { return ftruncate(fd_.get(), size); }

zx_status_t UniqueFdWrapper::Sync() { return fsync(fd_.get()); }

}  // namespace fvm::host
