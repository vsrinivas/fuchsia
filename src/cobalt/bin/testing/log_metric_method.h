// Copyright 2022  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTING_LOG_METRIC_METHOD_H_
#define SRC_COBALT_BIN_TESTING_LOG_METRIC_METHOD_H_

#include <ostream>

namespace cobalt {

enum class LogMetricMethod {
  kDefault = 0,
  kLogOccurrence = 1,
  kLogInteger = 2,
  kLogIntegerHistogram = 3,
  kLogString = 4,
  kLogMetricEvents = 5,
  kLogCustomEvent = 6,
};

// Implementation of the stream operator for printing LogMetricMethods.
std::ostream& operator<<(std::ostream& os, LogMetricMethod metric_method);

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_TESTING_LOG_METRIC_METHOD_H_
