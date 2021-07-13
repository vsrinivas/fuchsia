// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/periodic_logger.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

#include <chrono>

namespace {

void LoggingThread(std::string message, zx::duration logging_interval,
                   std::future<void> should_stop) {
  bool message_printed = false;
  zx::time start_time = zx::clock::get_monotonic();

  while (true) {
    // Wait until the next logging interval or we are asked to finish up.
    std::future_status status =
        should_stop.wait_for(std::chrono::microseconds(logging_interval.to_usecs()));
    if (status == std::future_status::ready) {
      break;
    }

    // Print out a log message.
    zx::time now = zx::clock::get_monotonic();
    FX_LOGS(INFO) << message << ": Waiting... (" << (now - start_time).to_secs() << "s passed)";
    message_printed = true;
  }

  // Only print a final message if we already printed a progress message.
  if (message_printed) {
    int64_t rounded_secs = (zx::clock::get_monotonic() - start_time + zx::msec(500)).to_secs();
    FX_LOGS(INFO) << message << ": Finished after " << rounded_secs << "s.";
  }
}

}  // namespace

PeriodicLogger::PeriodicLogger(std::string message, zx::duration logging_interval) {
  Start(std::move(message), logging_interval);
}

void PeriodicLogger::Start(std::string message, zx::duration logging_interval) {
  // Stop any existing thread.
  Stop();

  // Print the message.
  FX_LOGS(INFO) << message;

  // Start a new thread.
  should_stop_ = std::promise<void>();
  logging_thread_.emplace(&LoggingThread, std::move(message), logging_interval,
                          should_stop_.get_future());
}

void PeriodicLogger::Stop() {
  if (logging_thread_) {
    should_stop_.set_value();
    logging_thread_->join();
    logging_thread_.reset();
  }
}

PeriodicLogger::~PeriodicLogger() { Stop(); }
