// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_

#include <string>
#include <vector>

#include "source_location.h"
#include "string_view.h"
#include "token.h"

namespace fidl {

class ErrorReporter {
public:
    ErrorReporter(bool warnings_as_errors = false)
        : warnings_as_errors_(warnings_as_errors) {}

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

    void ReportErrorWithSquiggle(const SourceLocation& location,
                                 StringView message);
    void ReportError(const SourceLocation& location, StringView message) {
        ReportError(&location, message);
    }
    void ReportError(const SourceLocation* maybe_location, StringView message);
    void ReportError(const Token& token, StringView message);
    void ReportError(StringView message);
    void ReportWarningWithSquiggle(const SourceLocation& location,
                                   StringView message);
    void ReportWarning(const SourceLocation& location, StringView message) {
        ReportWarning(&location, message);
    }
    void ReportWarning(const SourceLocation* maybe_location, StringView message);
    void PrintReports();
    Counts Checkpoint() const { return Counts(this); }
    const std::vector<std::string>& errors() const { return errors_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void set_warnings_as_errors(bool value) { warnings_as_errors_ = value; }

private:
    void AddError(std::string formatted_message);
    void AddWarning(std::string formatted_message);

    bool warnings_as_errors_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_ERROR_REPORTER_H_
