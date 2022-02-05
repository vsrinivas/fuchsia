// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/time_provider.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>

#include "src/developer/forensics/feedback/annotations/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/time.h"

namespace forensics::feedback {
namespace {

ErrorOr<std::string> GetUptime() {
  const auto uptime = FormatDuration(zx::nsec(zx_clock_get_monotonic()));
  if (!uptime) {
    FX_LOGS(ERROR) << "Got negative uptime from zx_clock_get_monotonic()";
    return Error::kBadValue;
  }

  return *uptime;
}

ErrorOr<std::string> GetUtcTime(timekeeper::Clock* clock) {
  const auto time = CurrentUtcTime(clock);
  if (!time) {
    FX_LOGS(ERROR) << "Error getting UTC time from timekeeper::Clock::Now()";
    return Error::kBadValue;
  }

  return *time;
}

}  // namespace

TimeProvider::TimeProvider(std::unique_ptr<timekeeper::Clock> clock) : clock_(std::move(clock)) {}

Annotations TimeProvider::Get() {
  return {
      {kDeviceUptimeKey, GetUptime()},
      {kDeviceUtcTimeKey, GetUtcTime(clock_.get())},
  };
}

}  // namespace forensics::feedback
