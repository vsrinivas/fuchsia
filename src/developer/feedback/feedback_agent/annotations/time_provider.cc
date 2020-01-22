// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/feedback_agent/annotations/time_provider.h"

#include <lib/zx/time.h>

#include <optional>
#include <string>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/time.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"
#include "src/lib/timekeeper/clock.h"

namespace feedback {
namespace {

using fuchsia::feedback::Annotation;
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

TimeProvider::TimeProvider(const std::set<std::string>& annotations_to_get,
                           std::unique_ptr<Clock> clock)
    : annotations_to_get_(annotations_to_get), clock_(std::move(clock)) {
  const auto supported_annotations = GetSupportedAnnotations();
  for (const auto& annotation : annotations_to_get_) {
    FX_CHECK(supported_annotations.find(annotation) != supported_annotations.end());
  }
}

std::set<std::string> TimeProvider::GetSupportedAnnotations() {
  return {
      kAnnotationDeviceUptime,
      kAnnotationDeviceUTCTime,
  };
}

fit::promise<std::vector<Annotation>> TimeProvider::GetAnnotations() {
  std::vector<Annotation> annotations;

  std::optional<std::string> annotation_value;
  for (const auto& annotation_key : annotations_to_get_) {
    if (annotation_key == kAnnotationDeviceUptime) {
      annotation_value = GetUptime();
    } else if (annotation_key == kAnnotationDeviceUTCTime) {
      annotation_value = GetUTCTime(*clock_);
    }

    if (annotation_value) {
      Annotation annotation;
      annotation.key = annotation_key;
      annotation.value = std::move(annotation_value.value());

      annotations.push_back(std::move(annotation));
    } else {
      FX_LOGS(WARNING) << "Failed to build annotation " << annotation_key;
    }
  }
  return fit::make_ok_promise(annotations);
}

}  // namespace feedback
