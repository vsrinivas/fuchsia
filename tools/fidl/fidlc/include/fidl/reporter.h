// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_REPORTER_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_REPORTER_H_

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
#include "utils.h"

namespace fidl::reporter {

using diagnostics::Diagnostic;
using diagnostics::ErrorDef;
using diagnostics::WarningDef;
using utils::identity_t;

std::string Format(std::string_view qualifier, const std::optional<SourceSpan>& span,
                   std::string_view message, bool color, size_t squiggle_size = 0u);

class Reporter {
 public:
  explicit Reporter(bool warnings_as_errors = false, bool enable_color = false)
      : warnings_as_errors_(warnings_as_errors), enable_color_(enable_color) {}

  class Counts {
   public:
    explicit Counts(const Reporter* reporter)
        : reporter_(reporter),
          num_errors_(reporter->errors().size()),
          num_warnings_(reporter->warnings().size()) {}
    bool NoNewErrors() { return num_errors_ == reporter_->errors().size(); }
    bool NoNewWarnings() { return num_warnings_ == reporter_->warnings().size(); }

   private:
    const Reporter* reporter_;
    const size_t num_errors_;
    const size_t num_warnings_;
  };

  // TODO(fxbug.dev/90095): Remove these.
  template <typename... Args>
  static std::unique_ptr<Diagnostic> MakeError(const ErrorDef<Args...>& err,
                                               const std::optional<SourceSpan>& span,
                                               const identity_t<Args>&... args) {
    return Diagnostic::MakeError(err, span, args...);
  }
  template <typename... Args>
  static std::unique_ptr<Diagnostic> MakeError(const ErrorDef<Args...>& err,
                                               const identity_t<Args>&... args) {
    return Diagnostic::MakeError(err, std::nullopt, args...);
  }
  template <typename... Args>
  static std::unique_ptr<Diagnostic> MakeWarning(const WarningDef<Args...>& warn,
                                                 const std::optional<SourceSpan>& span,
                                                 const identity_t<Args>&... args) {
    return Diagnostic::MakeWarning(warn, span, args...);
  }
  template <typename... Args>
  static std::unique_ptr<Diagnostic> MakeWarning(const WarningDef<Args...>& warn,
                                                 const identity_t<Args>&... args) {
    return Diagnostic::MakeWarning(warn, std::nullopt, args...);
  }

  template <typename... Args>
  void Report(const ErrorDef<Args...>& err, const identity_t<Args>&... args) {
    Report(std::move(MakeError(err, args...)));
  }
  template <typename... Args>
  void Report(const ErrorDef<Args...>& err, const std::optional<SourceSpan>& span,
              const identity_t<Args>&... args) {
    Report(std::move(MakeError(err, span, args...)));
  }
  template <typename... Args>
  void Report(const ErrorDef<Args...>& err, const Token& token, const identity_t<Args>&... args) {
    Report(std::move(MakeError(err, token.span(), args...)));
  }

  template <typename... Args>
  void Report(const WarningDef<Args...>& err, const identity_t<Args>&... args) {
    Report(std::move(MakeWarning(err, args...)));
  }
  template <typename... Args>
  void Report(const WarningDef<Args...>& warn, const std::optional<SourceSpan>& span,
              const identity_t<Args>&... args) {
    Report(std::move(MakeWarning(warn, span, args...)));
  }
  template <typename... Args>
  void Report(const WarningDef<Args...>& err, const Token& token, const identity_t<Args>&... args) {
    Report(std::move(MakeWarning(err, token.span(), args...)));
  }

  // Reports an error or warning.
  void Report(std::unique_ptr<Diagnostic> diag);

  // Combines errors and warnings and sorts by (file, span).
  std::vector<Diagnostic*> Diagnostics() const;
  // Prints a report based on Diagnostics() in text format, with ANSI color
  // escape codes if color is enabled.
  void PrintReports() const;
  // Prints a report based on Diagnostics() in JSON format.
  void PrintReportsJson() const;
  // Creates a checkpoint. This lets you detect if new errors or warnings have
  // been added since the checkpoint.
  Counts Checkpoint() const { return Counts(this); }

  const std::vector<std::unique_ptr<Diagnostic>>& errors() const { return errors_; }
  const std::vector<std::unique_ptr<Diagnostic>>& warnings() const { return warnings_; }
  void set_warnings_as_errors(bool value) { warnings_as_errors_ = value; }

 private:
  void AddError(std::unique_ptr<Diagnostic> error);
  void AddWarning(std::unique_ptr<Diagnostic> warning);

  bool warnings_as_errors_;
  bool enable_color_;

  // The reporter collects error and warnings separately so that we can easily
  // keep track of the current number of errors during compilation. The number
  // of errors is used to determine whether the parser is in an `Ok` state.
  std::vector<std::unique_ptr<Diagnostic>> errors_;
  std::vector<std::unique_ptr<Diagnostic>> warnings_;
};

}  // namespace fidl::reporter

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_REPORTER_H_
