// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_INTEGRATION_LOGGER_H_
#define GARNET_BIN_GUEST_INTEGRATION_LOGGER_H_

#include <string>

// Logger is a singleton class that TestSerial uses to write the guest's logs
// to. Then a test listener outputs the buffer if a test fails.
class Logger {
 public:
  static Logger& Get();
  void Reset() { buffer_.clear(); }
  void Write(const char* s, size_t count);
  const std::string& Buffer() { return buffer_; }

 private:
  Logger() = default;

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  static constexpr bool kGuestOutput = false;
  std::string buffer_;
};

#endif  // GARNET_BIN_GUEST_INTEGRATION_LOGGER_H_
