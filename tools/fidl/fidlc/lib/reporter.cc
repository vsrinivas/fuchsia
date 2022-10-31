// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/reporter.h"

#include <zircon/assert.h>

#include <sstream>

#include "tools/fidl/fidlc/include/fidl/diagnostics_json.h"

namespace fidl {

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

// static
std::string Reporter::Format(std::string_view qualifier, SourceSpan span, std::string_view message,
                             bool color) {
  ZX_ASSERT_MSG(span.valid(), "diagnostic span must be valid");

  const std::string_view bold = color ? "\033[1m" : "";
  const std::string_view bold_red = color ? "\033[1;31m" : "";
  const std::string_view bold_green = color ? "\033[1;32m" : "";
  const std::string_view reset = color ? "\033[0m" : "";

  SourceFile::Position position;
  std::string surrounding_line = std::string(span.SourceLine(&position));
  ZX_ASSERT_MSG(surrounding_line.find('\n') == std::string::npos,
                "a single line should not contain a newline character");

  std::string squiggle = MakeSquiggle(surrounding_line, position.column);

  // If the span is size 0 (ie, something is completely missing), highlight the entire surrounding
  // line.
  const auto squiggle_size = (span.data().empty() ? surrounding_line.size() : span.data().size());
  squiggle += std::string(squiggle_size - 1, '~');

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
  error << bold << span.position_str() << ": " << reset;
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
  ZX_ASSERT_MSG(diag, "should not report nullptr diagnostic");
  switch (diag->get_severity()) {
    case DiagnosticKind::kError:
      AddError(std::move(diag));
      break;
    case DiagnosticKind::kWarning:
      AddWarning(std::move(diag));
      break;
    case DiagnosticKind::kRetired:
      assert(false &&
             "this diagnostic kind must never be shown - it only reserves retired error numerals");
      break;
  }
}

std::vector<Diagnostic*> Reporter::Diagnostics() const {
  std::vector<Diagnostic*> diagnostics;
  diagnostics.reserve(errors_.size() + warnings_.size());
  for (const auto& err : errors_) {
    diagnostics.push_back(err.get());
  }
  for (const auto& warn : warnings_) {
    diagnostics.push_back(warn.get());
  }

  // Sort by file > position > kind (errors then warnings) > sequentially by error id.
  sort(diagnostics.begin(), diagnostics.end(), [](Diagnostic* a, Diagnostic* b) -> bool {
    // SourceSpan overloads the < operator to compare by filename, then
    // start position, then end position.
    if (a->span < b->span)
      return true;
    if (b->span < a->span)
      return false;

    // If neither diagnostic had a span, or if their spans were ==, sort
    // by kind (errors first) and then message.
    if (a->get_severity() == DiagnosticKind::kError &&
        b->get_severity() == DiagnosticKind::kWarning)
      return true;
    if (a->get_severity() == DiagnosticKind::kWarning &&
        b->get_severity() == DiagnosticKind::kError)
      return false;
    return a->get_id() < b->get_id();
  });

  return diagnostics;
}

void Reporter::PrintReports(bool enable_color) const {
  const auto diags = Diagnostics();
  for (const auto& diag : diags) {
    std::string qualifier = diag->get_severity() == DiagnosticKind::kError ? "error" : "warning";
    auto msg = Format(qualifier, diag->span, diag->Print(), enable_color);
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

}  // namespace fidl
