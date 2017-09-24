// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sstream>

#include "garnet/bin/ui/scene_manager/util/print_op.h"
#include "lib/fxl/logging.h"

namespace scene_manager {

// Flexible error reporting with an ostream-like interface.  Subclasses must
// implement ReportError().
class ErrorReporter {
 public:
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

   private:
    // Only ErrorReporter can create reports.
    friend class ErrorReporter;
    Report(ErrorReporter* owner, fxl::LogSeverity severity);

    ErrorReporter* owner_;
    fxl::LogSeverity severity_;
    std::ostringstream stream_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Report);
  };

  // Create a new Report which will, upon destruction, invoke ReportError()
  // upon this ErrorReporter.
  Report INFO() { return Report(this, fxl::LOG_INFO); }
  Report WARN() { return Report(this, fxl::LOG_WARNING); }
  Report ERROR() { return Report(this, fxl::LOG_ERROR); }
  Report FATAL() { return Report(this, fxl::LOG_FATAL); }

  // Return a default ErrorReporter that is always available, which simply logs
  // the error using FXL_LOG(severity).
  static ErrorReporter* Default();

 private:
  virtual void ReportError(fxl::LogSeverity severity,
                           std::string error_string) = 0;
};

}  // namespace scene_manager
