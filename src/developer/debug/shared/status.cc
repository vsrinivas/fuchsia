// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/status.h"

#include <lib/syslog/cpp/macros.h>
#include <string.h>

namespace debug {

#if defined(__Fuchsia__)

Status ZxStatus(zx_status_t s) {
  if (s == ZX_OK)
    return Status();
  return Status(Status::InternalValues(), static_cast<int64_t>(s), zx_status_get_string(s));
}

Status ZxStatus(zx_status_t s, std::string msg) {
  if (s == ZX_OK)
    return Status();
  return Status(Status::InternalValues(), static_cast<int64_t>(s), std::move(msg));
}

#else

Status ErrnoStatus(int en) {
  if (en == 0)
    return Status();
  return Status(Status::InternalValues(), static_cast<int64_t>(en), strerror(en));
}

Status ErrnoStatus(int en, std::string msg) {
  if (en == 0)
    return Status();
  return Status(Status::InternalValues(), static_cast<int64_t>(en), std::move(msg));
}

#endif

Status::Status(std::string msg) : message_(std::move(msg)) { FX_DCHECK(!message_.empty()); }

}  // namespace debug
