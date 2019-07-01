// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/periodic_logger.h"

#include <lib/zx/time.h>
#include <src/lib/fxl/logging.h>

PeriodicLogger::PeriodicLogger(std::string operation, zx::duration logging_interval)
    : start_time_(zx::clock::get_monotonic()),
      operation_(std::move(operation)),
      logging_interval_(logging_interval),
      last_log_time_(start_time_) {}

PeriodicLogger::~PeriodicLogger() {
  // Only print a final message if we already printed a progress message.
  if (message_printed_) {
    FXL_LOG(INFO) << operation_ << ": Finished after "
                  << (zx::clock::get_monotonic() - start_time_).to_secs() << "s.";
  }
}

// Print a log message about the current operation if enough time has passed
// since the operation started / since the last log message.
void PeriodicLogger::LogIfRequired() {
  const zx::time now = zx::clock::get_monotonic();
  if (now - last_log_time_ >= logging_interval_) {
    FXL_LOG(INFO) << operation_ << ": Still waiting... (" << (now - start_time_).to_secs()
                  << "s passed)";
    last_log_time_ = now;
    message_printed_ = true;
  }
}
