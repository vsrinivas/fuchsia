// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_LOGGER_H_
#define SRC_VIRTUALIZATION_TESTS_LOGGER_H_

#include <zircon/compiler.h>

#include <mutex>
#include <string>
#include <string_view>

// Logger is a singleton class that GuestConsole uses to write the guest's logs
// to. Then a test listener outputs the buffer if a test fails.
//
// Thread safe.
class Logger {
 public:
  static Logger& Get();

  // Clear the log.
  void Reset();

  // Append the given string to the log.
  void Write(std::string_view buffer);

  // Return a copy of the current log.
  std::string Buffer() const;

  // Log all guest output immediately upon being received.
  //
  // If false, we only log guest output on test failure.
  //
  // TODO(fxbug.dev/56119): Currently enabled to diagnose ongoing test flakes.
  static constexpr bool kLogAllGuestOutput = true;

 private:
  Logger() = default;

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  mutable std::mutex mutex_;
  std::string buffer_ __TA_GUARDED(mutex_);
};

#endif  // SRC_VIRTUALIZATION_TESTS_LOGGER_H_
