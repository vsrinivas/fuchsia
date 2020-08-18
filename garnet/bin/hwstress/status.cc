// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "status.h"

#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include <memory>
#include <string>
#include <utility>

#include "src/lib/fxl/strings/string_printf.h"
#include "util.h"

namespace hwstress {

LogLevel LogLevelFromString(const std::string& value) {
  std::string value_lower(value);
  std::for_each(value_lower.begin(), value_lower.end(), [](char& c) { c = std::tolower(c); });
  if (value_lower == "terse") {
    return LogLevel::kTerse;
  } else if (value_lower == "normal") {
    return LogLevel::kNormal;
  } else if (value_lower == "verbose") {
    return LogLevel::kVerbose;
  } else {
    return LogLevel::kInvalid;
  }
}

namespace {

// Return a copy of |s| with newlines stripped from it.
std::string StripNewlines(std::string_view s) {
  std::string result;
  result.reserve(s.size());
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '\n') {
      result.push_back(s[i]);
    }
  }
  return result;
}
}  // namespace

StatusLine::StatusLine(LogLevel level) : log_level_(level) {}

void StatusLine::Log(std::string_view s) {
  if (log_level_ == LogLevel::kTerse) {
    return;
  }
  // Remove any status already on the current line.
  ClearLineIfNeeded();

  // Log the current line, adding a trailing newline if needed.
  printf("%*s", static_cast<int>(s.size()), s.data());
  if (s.back() != '\n') {
    printf("\n");
  }
  fflush(stdout);

  // Re-display the current status.
  PrintStatus();
}

void StatusLine::Log(const char* fmt, va_list ap) {
  std::string s = fxl::StringVPrintf(fmt, ap);
  Log(std::string_view(s.data(), s.size()));
}

void StatusLine::Log(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Log(fmt, ap);
  va_end(ap);
}

void StatusLine::Set(std::string_view status) {
  // If the new value matches the old, we have nothing to do.
  if (status == current_status_ || log_level_ == LogLevel::kTerse) {
    return;
  }

  // Otherwise, clear off the old status and print out the new.
  ClearLineIfNeeded();
  current_status_ = StripNewlines(status);
  PrintStatus();
}

void StatusLine::Set(const char* fmt, va_list ap) {
  std::string s = fxl::StringVPrintf(fmt, ap);
  Set(std::string_view(s.data(), s.size()));
}

void StatusLine::Set(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  Set(fmt, ap);
  va_end(ap);
}

void StatusLine::Verbose(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  if (log_level_ == LogLevel::kVerbose) {
    Log(fmt, ap);
  }
  va_end(ap);
}

void StatusLine::Verbose(std::string_view status) {
  if (log_level_ == LogLevel::kVerbose) {
    Log(status);
  }
}

// Remove the status line from the console.
void StatusLine::ClearLineIfNeeded() {
  if (!line_needs_clear_) {
    return;
  }
  // "\r" and ANSI escape code for clearing the current line.
  printf("\r\033[2K");
  line_needs_clear_ = false;
}

// Print |current_status_| to the console.
void StatusLine::PrintStatus() {
  if (current_status_.empty()) {
    return;
  }
  printf("%s", current_status_.c_str());
  fflush(stdout);
  line_needs_clear_ = true;
}

}  // namespace hwstress
