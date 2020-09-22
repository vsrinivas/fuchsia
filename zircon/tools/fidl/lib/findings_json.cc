// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/findings_json.h"

namespace fidl {

void FindingsJson::Generate(const Finding& finding) {
  GenerateObject([&]() {
    GenerateObjectMember("category", "fidl-lint/" + finding.subcategory(), Position::kFirst);
    GenerateObjectMember("message", finding.message());
    // TODO(fxbug.dev/7990) Add "url" to related FIDL documentation, per Tricium spec
    Generate(finding.span());
    std::vector<SuggestionWithReplacementSpan> suggestions;
    if (finding.suggestion().has_value()) {
      suggestions.emplace_back(
          SuggestionWithReplacementSpan{finding.span(), finding.suggestion().value()});
    }
    GenerateObjectMember("suggestions", suggestions);
  });
}

void FindingsJson::Generate(const SuggestionWithReplacementSpan& suggestion_with_span) {
  auto& suggestion = suggestion_with_span.suggestion;
  auto& span = suggestion_with_span.span;
  GenerateObject([&]() {
    GenerateObjectMember("description", suggestion.description(), Position::kFirst);
    std::vector<Replacement> replacements;
    if (suggestion.replacement().has_value()) {
      replacements.emplace_back(Replacement{span, suggestion.replacement().value()});
    }
    GenerateObjectMember("replacements", replacements);
  });
}

void FindingsJson::Generate(const Replacement& replacement) {
  GenerateObject([&]() {
    GenerateObjectMember("replacement", replacement.replacement, Position::kFirst);
    Generate(replacement.span);
  });
}

void FindingsJson::Generate(const SourceSpan& span) {
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

std::ostringstream FindingsJson::Produce() {
  ResetIndentLevel();

  GenerateArray(findings_);

  return std::move(json_file_);
}

}  // namespace fidl
