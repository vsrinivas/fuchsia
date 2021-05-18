// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_UNWINDER_ERROR_H_
#define SRC_DEVELOPER_DEBUG_UNWINDER_ERROR_H_

#include <string>
#include <variant>

#include "src/lib/fxl/strings/string_printf.h"

namespace unwinder {

// Use fbl::String to avoid copying the immutable error message.
class Error {
 public:
  [[gnu::format(printf, 2, 3)]] explicit Error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    has_err_ = true;
    msg_ = fxl::StringVPrintf(fmt, ap);
    va_end(ap);
  }

  bool has_err() const { return has_err_; }
  bool ok() const { return !has_err(); }
  const std::string& msg() const { return msg_; }

 private:
  // Use Success() to create an explicit success.
  Error() = default;
  friend Error Success();

  bool has_err_ = false;
  std::string msg_;
};

// Special way to create a non-error Error object.
inline Error Success() { return Error(); }

}  // namespace unwinder

#endif  // SRC_DEVELOPER_DEBUG_UNWINDER_ERROR_H_
