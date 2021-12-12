// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_UTIL_ERROR_REPORTER_H_
#define SRC_UI_SCENIC_LIB_SCENIC_UTIL_ERROR_REPORTER_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <sstream>
#include <string>

#include "src/lib/fxl/macros.h"

namespace scenic_impl {

// Flexible error reporting with an ostream-like interface.  Subclasses must
// implement ReportError().
class ErrorReporter {
 public:
  virtual ~ErrorReporter() = default;

  // Helper class with RAII semantics.  Invokes ErrorReporter::ReportError()
  // upon destruction.
  class Report {
   public:
    Report(Report&& report);
    ~Report();

    // Append the value to the output stream.
    template <typename T>
    Report& operator<<(const T& val) {
      stream_ << val;
      return *this;
    }

    Report& operator<<(const zx::time& val) {
      stream_ << val.get();
      return *this;
    }

    Report& operator<<(const zx::duration& val) {
      stream_ << val.get();
      return *this;
    }

   private:
    // Only ErrorReporter can create reports.
    friend class ErrorReporter;
    Report(ErrorReporter* owner, syslog::LogSeverity severity, const std::string& prefix);

    ErrorReporter* owner_ = nullptr;
    syslog::LogSeverity severity_;
    std::ostringstream stream_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Report);
  };

  // Create a new Report which will, upon destruction, invoke ReportError()
  // upon this ErrorReporter.
  Report INFO() { return Report(this, syslog::LOG_INFO, prefix_); }
  Report WARN() { return Report(this, syslog::LOG_WARNING, prefix_); }
  Report ERROR() { return Report(this, syslog::LOG_ERROR, prefix_); }
  Report FATAL() { return Report(this, syslog::LOG_FATAL, prefix_); }

  // Return a default ErrorReporter that is always available, which simply logs
  // the error using FX_LOGS(severity).
  static const std::shared_ptr<ErrorReporter>& Default();

  static std::unique_ptr<ErrorReporter> DefaultUnique();

  // Set a string that is printed before everything else.
  void SetPrefix(const std::string& prefix) { prefix_ = prefix; }

 private:
  virtual void ReportError(syslog::LogSeverity severity, std::string error_string) = 0;
  std::string prefix_;
};
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_UTIL_ERROR_REPORTER_H_
