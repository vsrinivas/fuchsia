// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/diagnostics_json.h"

#include "fidl/error_types.h"

namespace fidl {

void DiagnosticsJson::Generate(const Diagnostic& diagnostic) {
  GenerateObject([&]() {
    std::string category = diagnostic.type == DiagnosticType::kError ? "error" : "warning";
    GenerateObjectMember("category", "fidlc/" + category, Position::kFirst);

    GenerateObjectMember("message", diagnostic.error_or_warning->msg);
    if (diagnostic.error_or_warning->span)
      Generate(diagnostic.error_or_warning->span.value());
  });
}

void DiagnosticsJson::Generate(const SourceSpan& span) {
  GenerateObjectMember("path", span.source_file().filename());

  auto start = span.data();
  auto end = span.data();
  end.remove_prefix(start.size());

  auto end_span = SourceSpan(end, span.source_file());

  SourceFile::Position start_position = span.position();
  SourceFile::Position end_position = end_span.position();

  GenerateObjectMember("start_line", static_cast<uint32_t>(start_position.line));
  GenerateObjectMember("start_char", static_cast<uint32_t>(start_position.column - 1));
  GenerateObjectMember("end_line", static_cast<uint32_t>(end_position.line));
  GenerateObjectMember("end_char", static_cast<uint32_t>(end_position.column - 1));
}

std::ostringstream DiagnosticsJson::Produce() {
  ResetIndentLevel();

  std::vector<Diagnostic> diags;
  for (auto&& error : errors_) {
    diags.push_back(Diagnostic{.type = DiagnosticType::kError, .error_or_warning = error.get()});
  }
  for (auto&& warning : warnings_) {
    diags.push_back(
        Diagnostic{.type = DiagnosticType::kWarning, .error_or_warning = warning.get()});
  }

  GenerateArray(diags);

  return std::move(json_file_);
}

}  // namespace fidl
