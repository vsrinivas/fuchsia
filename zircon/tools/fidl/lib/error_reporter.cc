// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/error_reporter.h"

#include <cassert>
#include <sstream>

#include "fidl/token.h"

namespace fidl {
namespace error_reporter {

std::string MakeSquiggle(const std::string& surrounding_line, int column) {
  std::string squiggle;
  size_t line_size = surrounding_line.size();
  for (size_t i = 0; i < (static_cast<size_t>(column) - 1); i++) {
    if (i < line_size && surrounding_line[i] == '\t') {
      squiggle.push_back('\t');
    } else {
      squiggle.push_back(' ');
    }
  }
  squiggle.push_back('^');
  return squiggle;
}

std::string Format(std::string qualifier, const std::optional<SourceSpan>& span,
                   std::string_view message, bool color, size_t squiggle_size) {
  const std::string_view bold       = color ? "\033[1m"    : "";
  const std::string_view bold_red   = color ? "\033[1;31m" : "";
  const std::string_view bold_green = color ? "\033[1;32m" : "";
  const std::string_view reset      = color ? "\033[0m"    : "";

  if (!span) {
    std::stringstream error;
    error << bold_red << qualifier << ": " << reset;
    error << bold << message << reset;
    return error.str();
  }

  SourceFile::Position position;
  std::string surrounding_line = std::string(span->SourceLine(&position));
  assert(surrounding_line.find('\n') == std::string::npos &&
         "A single line should not contain a newline character");

  std::string squiggle = MakeSquiggle(surrounding_line, position.column);
  if (squiggle_size != 0u) {
    --squiggle_size;
  }
  squiggle += std::string(squiggle_size, '~');

  // Some tokens (like string literals) can span multiple lines. Truncate the
  // string to just one line at most.
  //
  // The +1 allows for squiggles at the end of line, which is useful when
  // referencing the bounds of a file or line (e.g. unexpected end of file,
  // expected something on an empty line).
  size_t line_size = surrounding_line.size() + 1;
  if (squiggle.size() > line_size) {
    squiggle.resize(line_size);
  }

  std::stringstream error;
  // Many editors and IDEs recognize errors in the form of
  // filename:linenumber:column: error: descriptive-test-here\n
  error << bold << span->position_str() << ": " << reset;
  error << bold_red << qualifier << ": " << reset;
  error << bold << message << reset;
  error << '\n' << surrounding_line << '\n';
  error << bold_green << squiggle << reset;
  return error.str();
}

void ErrorReporter::AddError(std::unique_ptr<BaseError> err) {
  if (mode_ == ReportingMode::kDoNotReport)
    return;
  errors_.push_back(std::move(err));
}

void ErrorReporter::AddWarning(std::unique_ptr<BaseError> warn) {
  if (mode_ == ReportingMode::kDoNotReport)
    return;
  if (warnings_as_errors_) {
    errors_.push_back(std::move(warn));
  } else {
    warnings_.push_back(std::move(warn));
  }
}

// Record an error with the span, message, source line, position indicator,
// and, if span is not nullopt, tildes under the token reported.
//
//     filename:line:col: {error, warning}: message
//     sourceline
//        ^~~~
void ErrorReporter::ReportError(std::unique_ptr<BaseError> err) {
  assert(err && "should not report nullptr error");
  AddError(std::move(err));
}
void ErrorReporter::ReportWarning(std::unique_ptr<BaseError> warn) {
  assert(warn && "should not report nullptr warning");
  AddWarning(std::move(warn));
}

void ErrorReporter::PrintReports() {
  for (const auto& error : errors_) {
    size_t squiggle_size = error->span ? error->span.value().data().size() : 0;
    auto error_str = Format("error", error->span, error->msg, enable_color_, squiggle_size);
    fprintf(stderr, "%s\n", error_str.c_str());
  }
  for (const auto& warning : warnings_) {
    size_t squiggle_size = warning->span ? warning->span.value().data().size() : 0;
    auto warning_str = Format("warning", warning->span, warning->msg,
                              enable_color_, squiggle_size);
    fprintf(stderr, "%s\n", warning_str.c_str());
  }
  if (!errors_.empty() && warnings_.empty()) {
    fprintf(stderr, "%zu error(s) reported.\n", errors_.size());
  } else if (errors_.empty() && !warnings_.empty()) {
    fprintf(stderr, "%zu warning(s) reported.\n", warnings_.size());
  } else if (!errors_.empty() && !warnings_.empty()) {
    fprintf(stderr, "%zu error(s) and %zu warning(s) reported.\n",
            errors_.size(), warnings_.size());
  }
}

}  // namespace error_reporter
}  // namespace fidl
