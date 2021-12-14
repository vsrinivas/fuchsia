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
#include <utility>
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

std::string Format(std::string_view qualifier, std::optional<SourceSpan> span,
                   std::string_view message, bool color, size_t squiggle_size = 0u);

class Reporter {
 public:
  Reporter() = default;
  Reporter(const Reporter&) = delete;
  Reporter(Reporter&&) = default;

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

  template <typename... Args>
  bool Fail(const ErrorDef<Args...>& def, SourceSpan span, const identity_t<Args>&... args) {
    Report(Diagnostic::MakeError(def, span, args...));
    return false;
  }

  // TODO(fxbug.dev/89213): Remove, all failures should report spans. There is
  // one error ErrIncludeCycle for which a major change is required to report
  // with appropriate span information, but other cases should be relatively
  // direct to improve.
  template <typename... Args>
  bool FailNoSpan(const ErrorDef<Args...>& def, const identity_t<Args>&... args) {
    Report(Diagnostic::MakeError(def, std::nullopt, args...));
    return false;
  }

  template <typename... Args>
  void Warn(const WarningDef<Args...>& def, SourceSpan span, const identity_t<Args>&... args) {
    Report(Diagnostic::MakeWarning(def, span, args...));
  }

  // Reports an error or warning.
  void Report(std::unique_ptr<Diagnostic> diag);

  // Combines errors and warnings and sorts by (file, span).
  std::vector<Diagnostic*> Diagnostics() const;
  // Prints a report based on Diagnostics() in text format, with ANSI color
  // escape codes if enable_color is true.
  void PrintReports(bool enable_color) const;
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

  bool warnings_as_errors_ = false;

  // The reporter collects error and warnings separately so that we can easily
  // keep track of the current number of errors during compilation. The number
  // of errors is used to determine whether the parser is in an `Ok` state.
  std::vector<std::unique_ptr<Diagnostic>> errors_;
  std::vector<std::unique_ptr<Diagnostic>> warnings_;
};

// ReporterMixin enables classes to call certain Reporter methods unqualified.
// It is meant to be used with private or protected inheritance. For example:
//
//     class Foo : private ReporterMixin {
//         Foo(Reporter* r) : ReporterMixin(r) {}
//         void DoSomething() {
//             if (/* ... */) Fail(...);  // instead of reporter_->Fail(...);
//         }
//     };
//
// Note: All ReporterMixin methods must be const, otherwise classes using the
// mixin would not be able to call them in const contexts.
class ReporterMixin {
 public:
  explicit ReporterMixin(Reporter* reporter) : reporter_(reporter) {}

  Reporter* reporter() const { return reporter_; }

  void Report(std::unique_ptr<Diagnostic> diag) const { reporter_->Report(std::move(diag)); }

  template <typename... Args>
  bool Fail(const ErrorDef<Args...>& def, SourceSpan span, const identity_t<Args>&... args) const {
    return reporter_->Fail(def, span, args...);
  }

  // TODO(fxbug.dev/89213): Remove.
  template <typename... Args>
  bool FailNoSpan(const ErrorDef<Args...>& def, const identity_t<Args>&... args) const {
    return reporter_->FailNoSpan(def, args...);
  }

  template <typename... Args>
  void Warn(const WarningDef<Args...>& def, SourceSpan span,
            const identity_t<Args>&... args) const {
    reporter_->Warn(def, span, args...);
  }

 private:
  Reporter* reporter_;
};

}  // namespace fidl::reporter

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_REPORTER_H_
