// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/time.h"

#include "src/lib/fxl/strings/string_printf.h"

namespace feedback {

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
}  // namespace feedback
