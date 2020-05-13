// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/diagnostics_json.h"

#include "fidl/diagnostic_types.h"

namespace fidl {

using diagnostics::Diagnostic;
using diagnostics::DiagnosticKind;

void DiagnosticsJson::Generate(const Diagnostic* diagnostic) {
  GenerateObject([&]() {
    std::string category = diagnostic->kind == DiagnosticKind::kError ? "error" : "warning";
    GenerateObjectMember("category", "fidlc/" + category, Position::kFirst);

    GenerateObjectMember("message", diagnostic->msg);
    if (diagnostic->span)
      Generate(diagnostic->span.value());
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

  GenerateArray(diagnostics_);

  return std::move(json_file_);
}

}  // namespace fidl
