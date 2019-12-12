// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_TESTING_LOG_METHOD_H_
#define SRC_COBALT_BIN_TESTING_LOG_METHOD_H_

namespace cobalt {

enum LogMethod {
  kOther = 0,
  kLogEvent = 1,
  kLogEventCount = 2,
  kLogElapsedTime = 3,
  kLogFrameRate = 4,
  kLogMemoryUsage = 5,
  kLogCustomEvent = 7,
  kLogCobaltEvents = 8,
  kLogCobaltEvent = 9,
  kLogIntHistogram = 10,
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_TESTING_LOG_METHOD_H_
