// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/types.h>
#include <zircon/assert.h>

#include <cstring>

#ifdef __Fuchsia__
#include <zircon/status.h>

std::string_view zxdump::Error::status_string() const { return zx_status_get_string(status_); }
#endif

namespace zxdump {
namespace {

struct PrintStatus {
  zx_status_t status_;
};

#ifdef __Fuchsia__

std::ostream& operator<<(std::ostream& os, PrintStatus error) {
  return os << zx_status_get_string(error.status_);
}

#else

std::ostream& operator<<(std::ostream& os, PrintStatus error) {
  return os << "error " << error.status_;
}

#endif

}  // namespace

std::string_view FdError::error_string() const { return strerror(error_); }

std::ostream& operator<<(std::ostream& os, const zxdump::Error& error) {
  ZX_DEBUG_ASSERT(!error.op_.empty());
  return os << error.op_ << ": " << PrintStatus{error.status_};
}

std::ostream& operator<<(std::ostream& os, const zxdump::FdError& fd_error) {
  ZX_DEBUG_ASSERT(!fd_error.op_.empty());
  if (fd_error.error_ == 0) {
    return os << fd_error.op_;
  }
  return os << fd_error.op_ << ": " << strerror(fd_error.error_);
}

}  // namespace zxdump
