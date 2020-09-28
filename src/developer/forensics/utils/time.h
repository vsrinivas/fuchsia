// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_TIME_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_TIME_H_

#include <lib/zx/time.h>

#include <optional>
#include <string>

#include "src/lib/timekeeper/clock.h"

namespace forensics {

// Formats the provided duration as WdXhYmZs e.g., 1d14h7m32s
std::optional<std::string> FormatDuration(zx::duration duration);

// Returns the non-localized current time according to |clock|.
std::optional<zx::time_utc> CurrentUTCTimeRaw(timekeeper::Clock* clock);

// Returns a non-localized human-readable timestamp of the current time according to |clock|.
std::optional<std::string> CurrentUTCTime(timekeeper::Clock* clock);

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_TIME_H_
