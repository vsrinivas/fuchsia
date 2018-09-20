// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <garnet/lib/overnet/environment/timer.h>
#include <garnet/lib/overnet/vocabulary/status.h>
#include <lib/async/cpp/time.h>
#include <zircon/status.h>
#include <sstream>

namespace overnetstack {

inline overnet::TimeStamp ToTimeStamp(zx::time t) {
  if (t == zx::time::infinite()) {
    return overnet::TimeStamp::AfterEpoch(overnet::TimeDelta::PositiveInf());
  }
  return overnet::TimeStamp::AfterEpoch(
      overnet::TimeDelta::FromMicroseconds(t.get() / 1000));
}

inline zx::time FromTimeStamp(overnet::TimeStamp t) {
  auto us = t.after_epoch().as_us();
  if (us < 0)
    return zx::time(0);
  if (us >= int64_t(ZX_TIME_INFINITE / 1000))
    return zx::time(ZX_TIME_INFINITE);
  return zx::time(us * 1000);
}

inline overnet::Status ToOvernetStatus(zx_status_t status) {
  switch (status) {
    case ZX_OK:
      return overnet::Status::Ok();
    case ZX_ERR_CANCELED:
      return overnet::Status::Cancelled();
    default: {
      std::ostringstream out;
      out << "zx_status:" << zx_status_get_string(status);
      return overnet::Status(overnet::StatusCode::UNKNOWN, out.str());
    }
  }
}

}  // namespace overnetstack
