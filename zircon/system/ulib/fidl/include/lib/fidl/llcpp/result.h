// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_RESULT_H_
#define LIB_FIDL_LLCPP_RESULT_H_

#include <zircon/types.h>

#ifdef __Fuchsia__
#include <zircon/status.h>
#endif  // !defined(_KERNEL)

namespace fidl {

// Class representing the result of an operation.
// If the operation was successful, |status()| returns ZX_OK and |error()| returns nullptr.
// If |status()| doesn't return ZX_OK the operation failed and |error()| returns a human-readable
// string for debugging purposes.
class Result {
 public:
  Result() = default;
  Result(zx_status_t status, const char* error) : status_(status), error_(error) {}
  explicit Result(const Result& result) {
    status_ = result.status_;
    error_ = result.error_;
  }

  [[nodiscard]] zx_status_t status() const { return status_; }
#ifdef __Fuchsia__
  // Returns the string representation of the status value.
  [[nodiscard]] const char* status_string() const { return zx_status_get_string(status_); }
#endif  // !defined(_KERNEL)
  [[nodiscard]] const char* error() const { return error_; }
  [[nodiscard]] bool ok() const { return status_ == ZX_OK; }

 protected:
  void SetResult(zx_status_t status, const char* error) {
    status_ = status;
    error_ = error;
  }

  zx_status_t status_ = ZX_ERR_INTERNAL;
  const char* error_ = nullptr;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_RESULT_H_
