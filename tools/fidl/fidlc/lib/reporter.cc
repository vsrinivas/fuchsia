// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/reporter.h"

#include <fidl/diagnostics_json.h>
#include <fidl/token.h>

#include <cassert>
#include <sstream>

namespace fidl::reporter {

using diagnostics::DiagnosticKind;

static std::string MakeSquiggle(std::string_view surrounding_line, int column) {
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

std::string Format(std::string_view qualifier, std::optional<SourceSpan> span,
                   std::string_view message, bool color, size_t squiggle_size) {
  const std::string_view bold = color ? "\033[1m" : "";
  const std::string_view bold_red = color ? "\033[1;31m" : "";
  const std::string_view bold_green = color ? "\033[1;32m" : "";
  const std::string_view reset = color ? "\033[0m" : "";

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

void Reporter::AddError(std::unique_ptr<Diagnostic> error) { errors_.push_back(std::move(error)); }

void Reporter::AddWarning(std::unique_ptr<Diagnostic> warning) {
  if (warnings_as_errors_) {
    errors_.push_back(std::move(warning));
  } else {
    warnings_.push_back(std::move(warning));
  }
}

// Record a diagnostic with the span, message, source line, position indicator,
// and, if span is not nullopt, tildes under the token reported.
//
//     filename:line:col: {error, warning}: message
//     sourceline
//        ^~~~
void Reporter::Report(std::unique_ptr<Diagnostic> diag) {
  assert(diag && "should not report nullptr diagnostic");
  switch (diag->kind) {
    case DiagnosticKind::kError:
      AddError(std::move(diag));
      break;
    case DiagnosticKind::kWarning:
      AddWarning(std::move(diag));
      break;
  }
}

std::vector<Diagnostic*> Reporter::Diagnostics() const {
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

void Reporter::PrintReports(bool enable_color) const {
  const auto diags = Diagnostics();
  for (const auto& diag : diags) {
    size_t squiggle_size = diag->span ? diag->span->data().size() : 0;
    std::string qualifier = diag->kind == DiagnosticKind::kError ? "error" : "warning";
    auto msg = Format(qualifier, diag->span, diag->msg, enable_color, squiggle_size);
    fprintf(stderr, "%s\n", msg.c_str());
  }

  if (!errors_.empty() && warnings_.empty()) {
    fprintf(stderr, "%zu error(s) reported.\n", errors_.size());
  } else if (errors_.empty() && !warnings_.empty()) {
    fprintf(stderr, "%zu warning(s) reported.\n", warnings_.size());
  } else if (!errors_.empty() && !warnings_.empty()) {
    fprintf(stderr, "%zu error(s) and %zu warning(s) reported.\n", errors_.size(),
            warnings_.size());
  }
}

void Reporter::PrintReportsJson() const {
  fprintf(stderr, "%s", fidl::DiagnosticsJson(Diagnostics()).Produce().str().c_str());
}

}  // namespace fidl::reporter
