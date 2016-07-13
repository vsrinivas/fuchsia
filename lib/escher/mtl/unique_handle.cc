// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <utility>

#include "mtl/unique_handle.h"

namespace mtl {

UniqueHandle::UniqueHandle(UniqueHandle&& other) : value_(other.value_) {
  other.value_ = MX_HANDLE_INVALID;
}

UniqueHandle& UniqueHandle::operator=(UniqueHandle&& other) {
  reset();
  std::swap(value_, other.value_);
  return *this;
}

}  // namespace mtl
