// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/error_reporter.h"

#include <cassert>

#include "fidl/flat_ast.h"
#include "fidl/names.h"
#include "fidl/raw_ast.h"
#include "fidl/source_span.h"
#include "fidl/token.h"

namespace fidl {

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

std::string FormatError(std::string qualifier, const std::optional<SourceSpan>& span,
                   std::string_view message, size_t squiggle_size) {
  if (!span) {
    std::string error = qualifier;
    error.append(": ");
    error.append(message);
    return error;
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

  // Many editors and IDEs recognize errors in the form of
  // filename:linenumber:column: error: descriptive-test-here\n
  std::string error = span->position_str();
  error.append(": ");
  error.append(qualifier);
  error.append(": ");
  error.append(message);
  error.push_back('\n');
  error.append(surrounding_line);
  error.push_back('\n');
  error.append(squiggle);
  return error;
}

namespace internal {

std::string Display(const std::string& s) { return s; }

std::string Display(std::string_view s) { return std::string(s); }

// {'A', 'B', 'C'} -> "A, B, C"
std::string Display(const std::set<std::string>& s) {
  std::stringstream ss;
  for (auto it = s.begin(); it != s.end(); it++) {
    if (it != s.cbegin()) {
      ss << ", ";
    }
    ss << *it;
  }
  return ss.str();
}

std::string Display(const SourceSpan& s) { return s.position_str(); }

std::string Display(const Token::KindAndSubkind& t) {
  return std::string(Token::Name(t));
}

std::string Display(const raw::Attribute& a) { return a.name; }

std::string Display(const raw::AttributeList& a) {
  std::stringstream attributes_found;
  for (auto it = a.attributes.begin(); it != a.attributes.end(); it++) {
    if (it != a.attributes.cbegin()) {
      attributes_found << ", ";
    }
    attributes_found << it->name;
  }
  return attributes_found.str();
}

std::string Display(const std::vector<std::string_view>& library_name) {
  return NameLibrary(library_name);
}

std::string Display(const flat::Constant* c) { return NameFlatConstant(c); }

std::string Display(const flat::TypeConstructor* tc) { return NameFlatTypeConstructor(tc); }

std::string Display(const flat::Type* t) { return NameFlatType(t); }

std::string Display(const flat::TypeTemplate* t) { return NameFlatName(t->name()); }

std::string Display(const flat::Name& n) { return std::string(n.full_name()); }

}  // namespace internal

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
//     filename:line:col: error: message
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

// Records a warning with the span, message, source line,
// position indicator, and tildes under the token reported.
//
//     filename:line:col: warning: message
//     sourceline
//        ^~~~
void ErrorReporter::ReportWarningWithSquiggle(const SourceSpan& span, std::string_view message) {
  auto token_data = span.data();
  auto warning = FormatError("warning", std::make_optional(span), message, token_data.size());
  string_warnings_.push_back(warning);
}

void ErrorReporter::PrintReports() {
  for (const auto& error : errors_) {
    size_t squiggle_size = error->span ? error->span.value().data().size() : 0;
    auto error_str = FormatError("error", error->span, error->Format(), squiggle_size);
    fprintf(stderr, "%s\n", error_str.c_str());
  }
  for (const auto& warning : warnings_) {
    size_t squiggle_size = warning->span ? warning->span.value().data().size() : 0;
    auto warning_str = FormatError("warning", warning->span, warning->Format(), squiggle_size);
    fprintf(stderr, "%s\n", warning_str.c_str());
  }
}

}  // namespace fidl
