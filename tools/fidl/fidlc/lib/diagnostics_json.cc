// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/diagnostics_json.h"

#include "tools/fidl/fidlc/include/fidl/diagnostic_types.h"

namespace fidl {

void DiagnosticsJson::Generate(const Diagnostic* diagnostic) {
  GenerateObject([&]() {
    std::string category =
        diagnostic->get_severity() == DiagnosticKind::kError ? "error" : "warning";
    GenerateObjectMember("category", "fidlc/" + category, Position::kFirst);

    GenerateObjectMember("error_id", diagnostic->PrintId());
    GenerateObjectMember("message", diagnostic->msg);
    Generate(diagnostic->span);
  });
}

void DiagnosticsJson::Generate(const SourceSpan& span) {
  GenerateObjectMember("path", span.source_file().filename());

  auto start = span.data();
  auto end = span.data();
  end.remove_prefix(start.size());

  // Gracefully handle a span indicating the end of the file.
  //
  // If the span starts at the end of the file, `end_span` should be the same as
  // `span`, or calling SourceSpan::position() will attempt to read past the end
  // of the source file.
  SourceSpan end_span;
  auto end_of_file = span.source_file().data().data() + span.source_file().data().size();
  if (start.data() == end_of_file) {
    end_span = span;
  } else {
    end_span = SourceSpan(end, span.source_file());
  }

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
