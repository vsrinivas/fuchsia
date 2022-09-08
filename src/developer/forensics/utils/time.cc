// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/time.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <ctime>

#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {

std::optional<std::string> FormatDuration(zx::duration duration) {
  if (duration == zx::duration::infinite()) {
    return "inf";
  }

  if (duration < zx::sec(0)) {
    return std::nullopt;
  }

  int64_t d = duration.to_hours() / 24;
  duration -= zx::hour(d * 24);

  int64_t h = duration.to_hours();
  duration -= zx::hour(h);

  int64_t m = duration.to_mins();
  duration -= zx::min(m);

  int64_t s = duration.to_secs();

  return fxl::StringPrintf("%ldd%ldh%ldm%lds", d, h, m, s);
}

timekeeper::time_utc CurrentUtcTimeRaw(timekeeper::Clock* clock) {
  timekeeper::time_utc now_utc;
  // UtcNow returns a non-OK status only if the underlying handle is bad or we don't have sufficient
  // rights to read the clock. CHECK-FAIL if this happens because the C++ runtime is expected to
  // properly set up a clock.
  if (const zx_status_t status = clock->UtcNow(&now_utc); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "Failed to get current Utc time";
  }

  return now_utc;
}

std::string CurrentUtcTime(timekeeper::Clock* clock) {
  timekeeper::time_utc now_utc = CurrentUtcTimeRaw(clock);
  // std::gmtime expects epoch in seconds.
  const int64_t now_utc_seconds = now_utc.get() / zx::sec(1).get();
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %X %Z", std::gmtime(&now_utc_seconds));
  return std::string(buffer);
}

}  // namespace forensics
