// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger.h"

#include <iostream>
#include <mutex>

Logger& Logger::Get() {
  static Logger logger;
  return logger;
}

void Logger::Write(std::string_view buffer) {
  {
    std::lock_guard<std::mutex> guard(mutex_);
    buffer_.append(buffer);
  }
  if (kLogAllGuestOutput) {
    std::cout << buffer;
    std::cout.flush();
  }
}

void Logger::Reset() {
  std::lock_guard<std::mutex> guard(mutex_);
  buffer_.clear();
}

std::string Logger::Buffer() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return buffer_;
}
