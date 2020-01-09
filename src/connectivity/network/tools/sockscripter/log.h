// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_LOG_H_
#define SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_LOG_H_

#include <iostream>
#include <sstream>

class LogMessageVoidify {
 public:
  void operator&(std::ostream& p) {}
};

class LogMessage {
 public:
  LogMessage(const char* file, int line) { stream_ << file << "[" << line << "]:"; }

  ~LogMessage() {
    stream_ << std::endl;
    std::cerr << stream_.str();
    std::cerr.flush();
  }

  std::ostream& stream() { return stream_; }

 private:
  std::ostringstream stream_;
};

#define LOG(severity) LogMessageVoidify() & LogMessage(__FILE__, __LINE__).stream()

#endif  // SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_LOG_H_
