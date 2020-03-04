// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/time_provider.h"

#include <lib/zx/time.h>

#include <optional>
#include <string>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/time.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/timekeeper/clock.h"

namespace feedback {
namespace {

using timekeeper::Clock;

std::optional<std::string> GetUptime() {
  const std::optional<std::string> uptime = FormatDuration(zx::nsec(zx_clock_get_monotonic()));
  if (!uptime) {
    FX_LOGS(ERROR) << "got negative uptime from zx_clock_get_monotonic()";
  }

  return uptime;
}

std::optional<std::string> GetUTCTime(const Clock& clock) {
  const std::optional<std::string> time = CurrentUTCTime(clock);
  if (!time) {
    FX_LOGS(ERROR) << "error getting UTC time from timekeeper::Clock::Now()";
  }

  return time;
}

}  // namespace

TimeProvider::TimeProvider(const AnnotationKeys& annotations_to_get, std::unique_ptr<Clock> clock)
    : annotations_to_get_(annotations_to_get), clock_(std::move(clock)) {
  const auto supported_annotations = GetSupportedAnnotations();
  for (const auto& annotation : annotations_to_get_) {
    FX_CHECK(supported_annotations.find(annotation) != supported_annotations.end());
  }
}

AnnotationKeys TimeProvider::GetSupportedAnnotations() {
  return {
      kAnnotationDeviceUptime,
      kAnnotationDeviceUTCTime,
  };
}

fit::promise<Annotations> TimeProvider::GetAnnotations() {
  Annotations annotations;

  for (const auto& key : annotations_to_get_) {
    std::optional<AnnotationValue> value;
    if (key == kAnnotationDeviceUptime) {
      value = GetUptime();
    } else if (key == kAnnotationDeviceUTCTime) {
      value = GetUTCTime(*clock_);
    }

    if (value) {
      annotations[key] = std::move(value.value());
    } else {
      FX_LOGS(WARNING) << "Failed to build annotation " << key;
    }
  }
  return fit::make_ok_promise(annotations);
}

}  // namespace feedback
