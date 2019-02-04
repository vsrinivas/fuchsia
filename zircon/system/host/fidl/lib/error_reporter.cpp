// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/error_reporter.h"
#include "fidl/source_location.h"
#include "fidl/string_view.h"
#include "fidl/token.h"

namespace fidl {

std::string MakeSquiggle(const std::string& surrounding_line, int column) {
    std::string squiggle;
    for (int i = 0; i < column; i++) {
        switch (surrounding_line[i]) {
        case '\t':
            squiggle.push_back('\t');
        default:
            squiggle.push_back(' ');
        }
    }
    squiggle.push_back('^');
    return squiggle;
}

std::string Format(std::string qualifier, const SourceLocation& location,
                   StringView message, size_t squiggle_size = 0u) {
    SourceFile::Position position;
    std::string surrounding_line = location.SourceLine(&position);

    std::string squiggle = MakeSquiggle(surrounding_line, position.column);
    if (squiggle_size != 0u) {
        --squiggle_size;
    }
    squiggle += std::string(squiggle_size, '~');
    // Some tokens (like string literals) can span multiple
    // lines. Truncate the string to just one line at most. The
    // containing line contains a newline, so drop it when
    // comparing sizes.
    size_t line_size = surrounding_line.size() - 1;
    if (squiggle.size() > line_size) {
        squiggle.resize(line_size);
    }

    // Many editors and IDEs recognize errors in the form of
    // filename:linenumber:column: error: descriptive-test-here\n
    std::string error = location.position();
    error.append(": ");
    error.append(qualifier);
    error.append(": ");
    error.append(message);
    error.push_back('\n');
    error.append(surrounding_line);
    error.append(squiggle);

    return error;
}

// ReportError records an error with the location, message, source line, and
// position indicator.
//
//     filename:line:col: error: message
//     sourceline
//        ^
void ErrorReporter::ReportError(const SourceLocation& location, StringView message) {
    auto error = Format("error", location, message);
    errors_.push_back(std::move(error));
}

// ReportError records an error with the location, message, source line,
// position indicator, and tildes under the token reported.
//
//     filename:line:col: error: message
//     sourceline
//        ^~~~
void ErrorReporter::ReportError(const Token& token, StringView message) {
    auto token_location = token.location();
    auto token_data = token_location.data();
    auto error = Format("error", token_location, message, token_data.size());
    errors_.push_back(std::move(error));
}

// ReportError records the provided message.
void ErrorReporter::ReportError(StringView message) {
    std::string error("error: ");
    error.append(message);
    errors_.push_back(std::move(error));
}

// ReportWarning records a warning with the location, message, source line, and
// position indicator.
//
//     filename:line:col: warning: message
//     sourceline
//        ^
void ErrorReporter::ReportWarning(const SourceLocation& location, StringView message) {
    if (warnings_as_errors_) {
        ReportError(location, message);
        return;
    }
    auto error = Format("warning", location, message);
    warnings_.push_back(std::move(error));
}

void ErrorReporter::PrintReports() {
    for (const auto& error : errors_) {
        fprintf(stderr, "%s\n", error.data());
    }
    for (const auto& warning : warnings_) {
        fprintf(stderr, "%s\n", warning.data());
    }
}

} // namespace fidl
