// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/types.h>
#include <zircon/assert.h>

#ifdef __Fuchsia__
#include <zircon/status.h>

std::string_view zxdump::Error::status_string() const { return zx_status_get_string(status_); }
#endif

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

std::ostream& operator<<(std::ostream& os, const zxdump::Error& error) {
  ZX_DEBUG_ASSERT(!error.op_.empty());
  return os << error.op_ << ": " << PrintStatus{error.status_};
}
