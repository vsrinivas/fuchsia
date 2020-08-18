// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_STATUS_H_
#define GARNET_BIN_HWSTRESS_STATUS_H_

#include <zircon/compiler.h>

#include <memory>
#include <string>
#include <string_view>

namespace hwstress {

// Level of log detail.
enum class LogLevel {
  kTerse,
  kNormal,
  kVerbose,
  kInvalid,
};

LogLevel LogLevelFromString(const std::string& value);

// Provides a simple console status line.
//
// Users can either |Set| an ephemeral status line (such as a progress
// bar or timer) or |Log| lines permanently to the console.
//
// Ephemeral status lines are automatically cleared when a new status
// line is provided or the class is destroyed.
//
// Thread compatible.
class StatusLine {
 public:
  explicit StatusLine(LogLevel level = LogLevel::kNormal);

  // Log the given string to console, ensuring that the current status
  // line is re-displayed afterwards.
  //
  // If |s| doesn't contain a trailing newline, one is added.
  void Log(std::string_view s);
  void Log(const char* fmt, ...) __PRINTFLIKE(2, 3);
  void Log(const char* fmt, va_list ap);

  // Update the current status line.
  void Set(std::string_view status);
  void Set(const char* fmt, ...) __PRINTFLIKE(2, 3);
  void Set(const char* fmt, va_list ap);

  // Print a verbose logging statement.
  void Verbose(std::string_view s);
  void Verbose(const char* fmt, ...) __PRINTFLIKE(2, 3);

 private:
  // Remove the status line from the console.
  void ClearLineIfNeeded();

  // Print |current_status_| to the console.
  void PrintStatus();

  // Last-printed status line.
  std::string current_status_;

  // If true, the line should be cleared before anything else is
  // printed.
  bool line_needs_clear_ = false;

  // Detail level of logs.
  LogLevel log_level_;
};

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_STATUS_H_
