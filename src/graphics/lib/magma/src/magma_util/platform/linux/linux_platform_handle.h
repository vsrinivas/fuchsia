// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LINUX_PLATFORM_HANDLE_H
#define LINUX_PLATFORM_HANDLE_H

#include "magma_util/macros.h"
#include "platform_handle.h"

namespace magma {

class LinuxPlatformHandle : public PlatformHandle {
 public:
  LinuxPlatformHandle(int file_descriptor) : fd_(file_descriptor) { DASSERT(fd_ >= 0); }

  ~LinuxPlatformHandle() { close(fd_); }

  bool GetCount(uint32_t* count_out) override { return DRETF(false, "Not supported"); }

  uint32_t release() override {
    DASSERT(fd_ >= 0);
    int fd = fd_;
    fd_ = -1;
    return fd;
  }

  int get() const { return fd_; }

 private:
  int fd_;
  static_assert(sizeof(fd_) == sizeof(uint32_t), "int is not 32 bits");
};

}  // namespace magma

#endif  // LINUX_PLATFORM_HANDLE_H
