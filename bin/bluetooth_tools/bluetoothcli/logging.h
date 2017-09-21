// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <iostream>

#include "lib/fxl/macros.h"

namespace bluetoothcli {

// Convenience wrapper class around std::cout. This doesn't do anything other than:
//   - insert automatic indentation in the beginning of a message;
//   - provide stream-line syntax around logging without requiring std::endl, which is input into
//     the stream automatically.
//
// Also see lib/fxl/logging.h for a similar but more involved implementation.
class LogMessage {
 public:
  explicit LogMessage(size_t indent_count);
  ~LogMessage();

  std::ostream& stream() { return std::cout; }

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(LogMessage);
};

}  // namespace bluetoothcli

#define CLI_LOG_STREAM(indent) ::bluetoothcli::LogMessage(indent).stream()

#define CLI_LOG_INDENT(indent) CLI_LOG_STREAM(indent)
#define CLI_LOG() CLI_LOG_INDENT(0)
