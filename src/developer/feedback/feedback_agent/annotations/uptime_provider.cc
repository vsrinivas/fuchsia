// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be // found in the LICENSE
// file.

#include "src/developer/feedback/feedback_agent/annotations/uptime_provider.h"

#include <lib/zx/time.h>

#include <optional>
#include <string>

#include "src/developer/feedback/feedback_agent/constants.h"
#include "src/developer/feedback/utils/time.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/syslog/cpp/logger.h"

namespace feedback {

using feedback::FormatDuration;
using fuchsia::feedback::Annotation;

UptimeProvider::UptimeProvider() : SingleSyncAnnotationProvider(kAnnotationDeviceUptime) {}

std::set<std::string> UptimeProvider::GetSupportedAnnotations() {
  return {
      kAnnotationDeviceUptime,
  };
}

std::optional<std::string> UptimeProvider::GetAnnotation() {
  const std::optional<std::string> uptime = FormatDuration(zx::nsec(zx_clock_get_monotonic()));
  if (!uptime) {
    FX_LOGS(ERROR) << "got negative uptime from zx_clock_get_monotonic()";
  }

  return uptime;
}

}  // namespace feedback
