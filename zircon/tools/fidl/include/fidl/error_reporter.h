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

constexpr std::string_view kFormatMarker = "{}";

std::string Display(std::string s);
std::string Display(flat::Constant* c);

inline std::string FormatErr(std::string_view msg) {
  // This assert should never fail, because FormatErr is only called by
  // ReportError -- and calls to ReportError fail at compile time if the # of
  // args passed in != the number of args in the Error definition.
  assert(msg.find(kFormatMarker) == std::string::npos &&
         "number of format string parameters '{}' != number of supplied arguments");
  return std::string(msg);
}

template <typename T, typename ...Rest>
std::string FormatErr(std::string_view msg, T t, Rest ...rest) {
  size_t i = msg.find(kFormatMarker);
  // This assert should never fail (see non-template FormatErr)
  assert(i != std::string::npos &&
         "number of format string parameters '{}' != number of supplied arguments");

  // Split string at marker, insert formatted parameter
  std::stringstream s;
  s << msg.substr(0, i) << Display(t)
    << msg.substr(i + kFormatMarker.length(), msg.length() - i - kFormatMarker.length());

  return FormatErr(s.str(), rest...);
}

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

  template <typename ...Args>
  void ReportError(const Error<Args...> err, const Args& ...args) {
    ReportErrorWithSpan(std::nullopt, FormatErr(err.msg, args...));
  }
  template <typename ...Args>
  void ReportError(const Error<Args...> err, const std::optional<SourceSpan>& span, const Args& ...args) {
    ReportErrorWithSpan(span, FormatErr(err.msg, args...));
  }

  void ReportErrorWithSpan(const std::optional<SourceSpan>& span,
                           std::string_view message) {
    size_t squiggle_size = span ? span.value().data().size() : 0;
    auto error = Format("error", span, message, squiggle_size);
    AddError(std::move(error));
  }

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
