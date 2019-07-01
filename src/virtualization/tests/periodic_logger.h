// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_PERIODIC_LOGGER_H_
#define SRC_VIRTUALIZATION_TESTS_PERIODIC_LOGGER_H_

#include <lib/zx/time.h>
#include <src/lib/fxl/logging.h>

// Print a log message every |logging_interval| units of time.
//
// Users should periodically call |LogIfRequired|. Once |logging_interval|
// has passed since class creation, a log message will be printed. Log
// messages will then continue to be printed every |logging_interval|.
class PeriodicLogger {
 public:
  PeriodicLogger(std::string operation, zx::duration logging_interval);
  ~PeriodicLogger();

  // Print a log message about the current operation if enough time has passed
  // since the operation started / since the last log message.
  void LogIfRequired();

 private:
  const zx::time start_time_;
  const std::string operation_;
  const zx::duration logging_interval_;
  bool message_printed_ = false;
  zx::time last_log_time_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_PERIODIC_LOGGER_H_
