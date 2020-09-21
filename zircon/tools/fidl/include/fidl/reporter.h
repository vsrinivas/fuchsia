// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_REPORTER_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_REPORTER_H_

#include <algorithm>
#include <cassert>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostics.h"
#include "source_span.h"
#include "token.h"

namespace fidl {
namespace reporter {

using diagnostics::Diagnostic;
using diagnostics::DiagnosticKind;
using diagnostics::Error;
using diagnostics::ErrorDef;
using diagnostics::Warning;
using diagnostics::WarningDef;

std::string MakeSquiggle(const std::string& surrounding_line, int column);

std::string Format(std::string qualifier, const std::optional<SourceSpan>& span,
                   std::string_view message, bool color, size_t squiggle_size = 0u);

class Reporter {
 public:
  Reporter(bool warnings_as_errors = false, bool enable_color = false)
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
    friend class Reporter;

    ScopedReportingMode(ReportingMode& source, ReportingMode value)
        : prev_value_(source), source_(source) {
      source_ = value;
    }

    ReportingMode prev_value_;
    ReportingMode& source_;
  };

  class Counts {
   public:
    Counts(const Reporter* reporter)
        : reporter_(reporter),
          num_errors_(reporter->errors().size()),
          num_warnings_(reporter->warnings().size()) {}
    bool NoNewErrors() { return num_errors_ == reporter_->errors().size(); }
    bool NoNewWarning() { return num_warnings_ == reporter_->warnings().size(); }

   private:
    const Reporter* reporter_;
    const size_t num_errors_;
    const size_t num_warnings_;
  };

  // Used to create a std::unique_ptr<Error> or std::unique_ptr<Warning> rather than
  // std::make_unique to avoid having to specify the Args... template parameters on Error
  // explicitly.
  template <typename... Args>
  static std::unique_ptr<Error<Args...>> MakeError(const ErrorDef<Args...>& err,
                                                   const std::optional<SourceSpan>& span,
                                                   Args... args) {
    return std::make_unique<Error<Args...>>(err, span, args...);
  }
  template <typename... Args>
  static std::unique_ptr<Error<Args...>> MakeError(const ErrorDef<Args...>& err, Args... args) {
    return std::make_unique<Error<Args...>>(err, std::nullopt, args...);
  }
  template <typename... Args>
  static std::unique_ptr<Warning<Args...>> MakeWarning(const WarningDef<Args...>& warn,
                                                       const std::optional<SourceSpan>& span,
                                                       Args... args) {
    return std::make_unique<Warning<Args...>>(warn, span, args...);
  }
  template <typename... Args>
  static std::unique_ptr<Warning<Args...>> MakeWarning(const WarningDef<Args...>& warn,
                                                       Args... args) {
    return std::make_unique<Warning<Args...>>(warn, std::nullopt, args...);
  }

  template <typename... Args>
  void Report(const ErrorDef<Args...>& err, const Args&... args) {
    Report(std::move(MakeError(err, args...)));
  }
  template <typename... Args>
  void Report(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
              const Args&... args) {
    Report(std::move(MakeError(err, span, args...)));
  }
  template <typename... Args>
  void Report(const ErrorDef<Args...>& err, const Token& token, const Args&... args) {
    Report(std::move(MakeError(err, token.span(), args...)));
  }

  template <typename... Args>
  void Report(const WarningDef<Args...>& err, const Args&... args) {
    Report(std::move(MakeWarning(err, args...)));
  }
  template <typename... Args>
  void Report(const WarningDef<Args...>& warn, const std::optional<SourceSpan>& span,
              const Args&... args) {
    Report(std::move(MakeWarning(warn, span, args...)));
  }
  template <typename... Args>
  void Report(const WarningDef<Args...>& err, const Token& token, const Args&... args) {
    Report(std::move(MakeWarning(err, token.span(), args...)));
  }

  void Report(std::unique_ptr<Diagnostic> diag);

  void PrintReports();
  void PrintReportsJson();
  Counts Checkpoint() const { return Counts(this); }
  ScopedReportingMode OverrideMode(ReportingMode mode_override) {
    return ScopedReportingMode(mode_, mode_override);
  }
  // Used to print reports. Combines errors and warnings and sorts by (file, span).
  std::vector<Diagnostic*> diagnostics() const {
    std::vector<Diagnostic*> diagnostics;
    for (const auto& err : errors_) {
      diagnostics.push_back(err.get());
    }
    for (const auto& warn : warnings_) {
      diagnostics.push_back(warn.get());
    }

    // Sort by file > position > kind (errors then warnings) > message.
    sort(diagnostics.begin(), diagnostics.end(), [](Diagnostic* a, Diagnostic* b) -> bool {
      if (a->span && b->span) {
        // SourceSpan overloads the < operator to compare by filename, then
        // start position, then end position.
        if (a->span < b->span)
          return true;
        if (b->span < a->span)
          return false;
      } else {
        // Sort errors without spans first.
        if (b->span)
          return true;
        if (a->span)
          return false;
      }

      // If neither diagnostic had a span, or if their spans were ==, sort
      // by kind (errors first) and then message.
      if (a->kind == DiagnosticKind::kError && b->kind == DiagnosticKind::kWarning)
        return true;
      if (a->kind == DiagnosticKind::kWarning && b->kind == DiagnosticKind::kError)
        return false;
      return a->msg < b->msg;
    });

    return diagnostics;
  }
  const std::vector<std::unique_ptr<Diagnostic>>& errors() const { return errors_; }
  const std::vector<std::unique_ptr<Diagnostic>>& warnings() const { return warnings_; }
  void set_warnings_as_errors(bool value) { warnings_as_errors_ = value; }

 private:
  void AddError(std::unique_ptr<Diagnostic> err);
  void AddWarning(std::unique_ptr<Diagnostic> warn);

  ReportingMode mode_ = ReportingMode::kReport;
  bool warnings_as_errors_;
  bool enable_color_;

  // The reporter collects error and warnings separately so that we can easily
  // keep track of the current number of errors during compilation. The number
  // of errors is used to determine whether the parser is in an `Ok` state.
  std::vector<std::unique_ptr<Diagnostic>> errors_;
  std::vector<std::unique_ptr<Diagnostic>> warnings_;
};

}  // namespace reporter
}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_REPORTER_H_
