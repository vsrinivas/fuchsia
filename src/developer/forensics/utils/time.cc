// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/time.h"

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

std::optional<zx::time_utc> CurrentUTCTimeRaw(timekeeper::Clock* clock) {
  zx::time_utc now_utc;
  if (const zx_status_t status = clock->Now(&now_utc); status != ZX_OK) {
    return std::nullopt;
  }

  return now_utc;
}

std::optional<std::string> CurrentUTCTime(timekeeper::Clock* clock) {
  auto now_utc = CurrentUTCTimeRaw(clock);
  if (!now_utc.has_value()) {
    return std::nullopt;
  }
  // std::gmtime expects epoch in seconds.
  const int64_t now_utc_seconds = now_utc.value().get() / zx::sec(1).get();
  char buffer[32];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %X %Z", std::gmtime(&now_utc_seconds));
  return std::string(buffer);
}

}  // namespace forensics
