// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_

#include <cassert>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "errors.h"
#include "source_span.h"
#include "token.h"

namespace fidl {

std::string MakeSquiggle(const std::string& surrounding_line, int column);

std::string Format(std::string qualifier, const std::optional<SourceSpan>& span,
                   std::string_view message, size_t squiggle_size = 0u);

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

  // Used to create a std::unique_ptr<ReportedError> rather than
  // std::make_unique to avoid having to specify the Args... template parameters
  // on ReportedError explicitly.
  template <typename... Args>
  static std::unique_ptr<ReportedError<Args...>> MakeReportedError(
      const Error<Args...>& err, const std::optional<SourceSpan>& span, Args... args) {
    return std::make_unique<ReportedError<Args...>>(err, span, args...);
  }
  template <typename... Args>
  static std::unique_ptr<ReportedError<Args...>> MakeReportedError(
      const Error<Args...>& err, Args... args) {
    return std::make_unique<ReportedError<Args...>>(err, std::nullopt, args...);
  }

  void ReportError(std::unique_ptr<BaseReportedError> err);
  template <typename... Args>
  void ReportError(const Error<Args...> err, const Args&... args) {
    ReportErrorWithSpan(std::nullopt, internal::FormatErr(err.msg, args...));
  }
  template <typename... Args>
  void ReportError(const Error<Args...> err, const std::optional<SourceSpan>& span,
                   const Args&... args) {
    ReportErrorWithSpan(span, internal::FormatErr(err.msg, args...));
  }
  template <typename ...Args>
  void ReportError(const Error<Args...> err, const Token& token, const Args& ...args) {
    ReportErrorWithSpan(token.span(), internal::FormatErr(err.msg, args...));
  }

  template <typename... Args>
  void ReportWarning(const Error<Args...> err, const std::optional<SourceSpan>& span,
                     const Args& ...args) {
    ReportWarningWithSpan(span, internal::FormatErr(err.msg, args...));
  }
  template <typename... Args>
  void ReportWarning(const Error<Args...> err, const Token& token, const Args& ...args) {
    ReportWarningWithSpan(token.span(), internal::FormatErr(err.msg, args...));
  }

  void ReportWarningWithSquiggle(const SourceSpan& span, std::string_view message);

  void PrintReports();
  Counts Checkpoint() const { return Counts(this); }
  ScopedReportingMode OverrideMode(ReportingMode mode_override) {
    return ScopedReportingMode(mode_, mode_override);
  }
  const std::vector<std::string>& errors() const { return errors_; }
  const std::vector<std::string>& warnings() const { return warnings_; }
  void set_warnings_as_errors(bool value) { warnings_as_errors_ = value; }

 private:
  void ReportErrorWithSpan(const std::optional<SourceSpan>& span, std::string_view message);
  void ReportWarningWithSpan(const std::optional<SourceSpan>& span, std::string_view message);
  void AddError(std::string formatted_message);
  void AddWarning(std::string formatted_message);

  ReportingMode mode_ = ReportingMode::kReport;
  bool warnings_as_errors_;
  std::vector<std::string> errors_;
  std::vector<std::string> warnings_;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_
