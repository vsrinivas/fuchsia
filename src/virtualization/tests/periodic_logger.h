// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_PERIODIC_LOGGER_H_
#define SRC_VIRTUALIZATION_TESTS_PERIODIC_LOGGER_H_

#include <lib/zx/time.h>
#include <zircon/compiler.h>

#include <future>
#include <optional>
#include <thread>

// Print a log message every |logging_interval| units of time.
//
// A thread will be started that will log the string |message| after
// |logging_interval| has passed, and then continue to print |message|
// every |logging_interval|.
class __WARN_UNUSED_CONSTRUCTOR PeriodicLogger {
 public:
  PeriodicLogger() = default;
  PeriodicLogger(std::string message, zx::duration logging_interval);
  ~PeriodicLogger();

  // Prevent copy and move.
  PeriodicLogger(const PeriodicLogger&) = delete;
  PeriodicLogger& operator=(const PeriodicLogger&) = delete;

  // Start logging the given message.
  //
  // If a message is already being logged, this new message and interval
  // will replace it.
  void Start(std::string message, zx::duration logging_interval);

  // Stop logging.
  void Stop();

 private:
  std::promise<void> should_stop_;
  std::optional<std::thread> logging_thread_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_PERIODIC_LOGGER_H_
