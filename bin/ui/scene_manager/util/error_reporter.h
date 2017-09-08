// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <sstream>

#include "apps/mozart/src/scene_manager/print_op.h"
#include "lib/ftl/logging.h"

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
    Report(ErrorReporter* owner, ftl::LogSeverity severity);

    ErrorReporter* owner_;
    ftl::LogSeverity severity_;
    std::ostringstream stream_;

    FTL_DISALLOW_COPY_AND_ASSIGN(Report);
  };

  // Create a new Report which will, upon destruction, invoke ReportError()
  // upon this ErrorReporter.
  Report INFO() { return Report(this, ftl::LOG_INFO); }
  Report WARN() { return Report(this, ftl::LOG_WARNING); }
  Report ERROR() { return Report(this, ftl::LOG_ERROR); }
  Report FATAL() { return Report(this, ftl::LOG_FATAL); }

  // Return a default ErrorReporter that is always available, which simply logs
  // the error using FTL_LOG(severity).
  static ErrorReporter* Default();

 private:
  virtual void ReportError(ftl::LogSeverity severity,
                           std::string error_string) = 0;
};

}  // namespace scene_manager
