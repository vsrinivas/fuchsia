// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MTL_UNIQUE_HANDLE_H_
#define MTL_UNIQUE_HANDLE_H_

#include <magenta/syscalls.h>

#include "ftl/macros.h"

namespace mtl {

class UniqueHandle {
 public:
  UniqueHandle() = default;
  explicit UniqueHandle(mx_handle_t value) : value_(value) {}
  ~UniqueHandle() { reset(); }

  UniqueHandle(UniqueHandle&& other);
  UniqueHandle& operator=(UniqueHandle&& other);

  mx_handle_t get() const { return value_; }

  bool is_valid() const { return value_ > MX_HANDLE_INVALID; }
  bool is_error() const { return value_ < MX_HANDLE_INVALID; }

  mx_handle_t release() {
    mx_handle_t result = value_;
    value_ = MX_HANDLE_INVALID;
    return result;
  }

  void reset(mx_handle_t value = MX_HANDLE_INVALID) {
    mx_handle_t previous = value_;
    value_ = value;
    if (previous > MX_HANDLE_INVALID)
      mx_handle_close(previous);
  }

 private:
  mx_handle_t value_ = MX_HANDLE_INVALID;

  FTL_DISALLOW_COPY_AND_ASSIGN(UniqueHandle);
};

}  // namespace mtl

#endif  // MTL_UNIQUE_HANDLE_H_
