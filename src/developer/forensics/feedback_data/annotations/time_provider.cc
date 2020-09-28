// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/annotations/time_provider.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>

#include "src/developer/forensics/feedback_data/annotations/utils.h"
#include "src/developer/forensics/feedback_data/constants.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/developer/forensics/utils/time.h"
#include "src/lib/timekeeper/clock.h"

namespace forensics {
namespace feedback_data {
namespace {

using timekeeper::Clock;

const AnnotationKeys kSupportedAnnotations = {
    kAnnotationDeviceUptime,
    kAnnotationDeviceUtcTime,
};

AnnotationOr GetUptime() {
  const auto uptime = FormatDuration(zx::nsec(zx_clock_get_monotonic()));
  if (!uptime) {
    FX_LOGS(ERROR) << "got negative uptime from zx_clock_get_monotonic()";
    return AnnotationOr(Error::kBadValue);
  }

  return AnnotationOr(*uptime);
}

AnnotationOr GetUtcTime(Clock* clock) {
  const auto time = CurrentUtcTime(clock);
  if (!time) {
    FX_LOGS(ERROR) << "error getting UTC time from timekeeper::Clock::Now()";
    return AnnotationOr(Error::kBadValue);
  }

  return AnnotationOr(*time);
}

}  // namespace

TimeProvider::TimeProvider(std::unique_ptr<Clock> clock) : clock_(std::move(clock)) {}

::fit::promise<Annotations> TimeProvider::GetAnnotations(zx::duration timeout,
                                                         const AnnotationKeys& allowlist) {
  const AnnotationKeys annotations_to_get = RestrictAllowlist(allowlist, kSupportedAnnotations);
  if (annotations_to_get.empty()) {
    return ::fit::make_result_promise<Annotations>(::fit::ok<Annotations>({}));
  }

  Annotations annotations;

  for (const auto& key : annotations_to_get) {
    if (key == kAnnotationDeviceUptime) {
      annotations.insert({key, GetUptime()});
    } else if (key == kAnnotationDeviceUtcTime) {
      annotations.insert({key, GetUtcTime(clock_.get())});
    }
  }

  return ::fit::make_ok_promise(annotations);
}

}  // namespace feedback_data
}  // namespace forensics
