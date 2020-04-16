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
namespace error_reporter {

using errors::BaseError;
using errors::Error;
using errors::ErrorDef;

std::string MakeSquiggle(const std::string& surrounding_line, int column);

std::string Format(std::string qualifier, const std::optional<SourceSpan>& span,
                   std::string_view message, bool color, size_t squiggle_size = 0u);

class ErrorReporter {
 public:
  ErrorReporter(bool warnings_as_errors = false, bool enable_color = false)
    : warnings_as_errors_(warnings_as_errors), enable_color_(enable_color) {}

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

  // Used to create a std::unique_ptr<Error> rather than
  // std::make_unique to avoid having to specify the Args... template parameters
  // on Error explicitly.
  template <typename... Args>
  static std::unique_ptr<Error<Args...>> MakeError(
      const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span, Args... args) {
    return std::make_unique<Error<Args...>>(err, span, args...);
  }
  template <typename... Args>
  static std::unique_ptr<Error<Args...>> MakeError(
      const ErrorDef<Args...>& err, Args... args) {
    return std::make_unique<Error<Args...>>(err, std::nullopt, args...);
  }

  template <typename... Args>
  void ReportError(const ErrorDef<Args...>& err, const Args&... args) {
    ReportError(std::move(MakeError(err, args...)));
  }
  template <typename... Args>
  void ReportError(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
                   const Args&... args) {
    ReportError(std::move(MakeError(err, span, args...)));
  }
  template <typename ...Args>
  void ReportError(const ErrorDef<Args...>& err, const Token& token, const Args& ...args) {
    ReportError(std::move(MakeError(err, token.span(), args...)));
  }

  void ReportError(std::unique_ptr<BaseError> err);

  template <typename... Args>
  void ReportWarning(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
                     const Args& ...args) {
    ReportWarning(std::move(MakeError(err, span, args...)));
  }
  template <typename... Args>
  void ReportWarning(const ErrorDef<Args...>& err, const Token& token, const Args& ...args) {
    ReportWarning(std::move(MakeError(err, token.span(), args...)));
  }

  void ReportWarning(std::unique_ptr<BaseError> err);

  void PrintReports();
  Counts Checkpoint() const { return Counts(this); }
  ScopedReportingMode OverrideMode(ReportingMode mode_override) {
    return ScopedReportingMode(mode_, mode_override);
  }
  const std::vector<std::unique_ptr<BaseError>>& warnings() const { return warnings_; }
  const std::vector<std::unique_ptr<BaseError>>& errors() const { return errors_; }
  void set_warnings_as_errors(bool value) { warnings_as_errors_ = value; }

 private:
  void AddError(std::unique_ptr<BaseError> err);
  void AddWarning(std::unique_ptr<BaseError> warn);

  ReportingMode mode_ = ReportingMode::kReport;
  bool warnings_as_errors_;
  bool enable_color_;
  std::vector<std::unique_ptr<BaseError>> warnings_;
  std::vector<std::unique_ptr<BaseError>> errors_;
};

}  // namespace error_reporter
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_
