// Copyright 2022  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/testing/log_metric_method.h"

namespace cobalt {

std::string LogMetricMethodToString(LogMetricMethod metric_method) {
  switch (metric_method) {
    case LogMetricMethod::kDefault:
      return "Default";
    case LogMetricMethod::kLogOccurrence:
      return "LogOccurrence";
    case LogMetricMethod::kLogInteger:
      return "LogInteger";
    case LogMetricMethod::kLogIntegerHistogram:
      return "LogIntegerHistogram";
    case LogMetricMethod::kLogString:
      return "LogString";
    case LogMetricMethod::kLogMetricEvents:
      return "LogMetricEvents";
    case LogMetricMethod::kLogCustomEvent:
      return "LogCustomEvent";
    default:
      return "Invalid LogMetricMethod";
  }
}

std::ostream& operator<<(std::ostream& os, LogMetricMethod metric_method) {
  return os << LogMetricMethodToString(metric_method);
}

}  // namespace cobalt
