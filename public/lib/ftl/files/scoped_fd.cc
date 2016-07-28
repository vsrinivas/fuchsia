// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/files/scoped_fd.h"

#include <unistd.h>

#include <utility>

namespace ftl {

ScopedFD::ScopedFD() : fd_(-1) {}

ScopedFD::ScopedFD(int fd) : fd_(fd) {}

ScopedFD::~ScopedFD() {
  Close();
}

ScopedFD::ScopedFD(ScopedFD&& other) : fd_(other.fd_) {
  other.fd_ = -1;
}

ScopedFD& ScopedFD::operator=(ScopedFD&& other) {
  Close();
  std::swap(fd_, other.fd_);
  return *this;
}

void ScopedFD::Close() {
  if (is_valid())
    close(fd_);
  fd_ = -1;
}

}  // namespace ftl
