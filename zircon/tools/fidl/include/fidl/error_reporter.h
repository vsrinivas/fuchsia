// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "source_span.h"
#include "token.h"

namespace fidl {

class ErrorReporter {
 public:
  ErrorReporter(bool warnings_as_errors = false) : warnings_as_errors_(warnings_as_errors) {}

  // Enables temporarily muting reporting.
  enum class ReportingMode {
    kReport,
    kDoNotReport,
  };

  // Controls a scoped override of the reporting mode of the error reporter.
  // Resets the mode to its previous value on destruction.
  class ScopedReportingMode {
   public:
    ~ScopedReportingMode() { source_ = prev_value_; }

   private:
    friend class ErrorReporter;

    ScopedReportingMode(ReportingMode& source, ReportingMode value)
        : prev_value_(source), source_(source) {
      source_ = value;
    }

    ReportingMode prev_value_;
    ReportingMode& source_;
  };

  class Counts {
   public:
    Counts(const ErrorReporter* reporter)
        : reporter_(reporter),
          num_errors_(reporter->errors().size()),
          num_warnings_(reporter->warnings().size()) {}
    bool NoNewErrors() { return num_errors_ == reporter_->errors().size(); }
    bool NoNewWarning() { return num_warnings_ == reporter_->warnings().size(); }

   private:
    const ErrorReporter* reporter_;
    const size_t num_errors_;
    const size_t num_warnings_;
  };

  void ReportErrorWithSquiggle(const SourceSpan& span, std::string_view message);
  void ReportError(const std::optional<SourceSpan>& span, std::string_view message);
  void ReportError(const Token& token, std::string_view message);
  void ReportError(std::string_view message);

  void ReportWarningWithSquiggle(const SourceSpan& span, std::string_view message);
  void ReportWarning(const std::optional<SourceSpan>& span, std::string_view message);
  void ReportWarning(const Token& token, std::string_view message);

  void PrintReports();
  Counts Checkpoint() const { return Counts(this); }
  ScopedReportingMode OverrideMode(ReportingMode mode_override) {
    return ScopedReportingMode(mode_, mode_override);
  }
  const std::vector<std::string>& errors() const { return errors_; }
  const std::vector<std::string>& warnings() const { return warnings_; }
  void set_warnings_as_errors(bool value) { warnings_as_errors_ = value; }

 private:
  void AddError(std::string formatted_message);
  void AddWarning(std::string formatted_message);

  ReportingMode mode_ = ReportingMode::kReport;
  bool warnings_as_errors_;
  std::vector<std::string> errors_;
  std::vector<std::string> warnings_;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_
