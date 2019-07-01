// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/findings_json.h"

namespace fidl {

void FindingsJson::Generate(const Finding& finding) {
  GenerateObject([&]() {
    GenerateObjectMember("category", "fidl-lint/" + finding.subcategory(), Position::kFirst);
    GenerateObjectMember("message", finding.message());
    // TODO(FIDL-668) Add "url" to related FIDL documentation, per Tricium spec
    Generate(finding.location());
    std::vector<SuggestionWithReplacementLocation> suggestions;
    if (finding.suggestion().has_value()) {
      suggestions.emplace_back(
          SuggestionWithReplacementLocation{finding.location(), finding.suggestion().value()});
    }
    GenerateObjectMember("suggestions", suggestions);
  });
}

void FindingsJson::Generate(const SuggestionWithReplacementLocation& suggestion_with_location) {
  auto& suggestion = suggestion_with_location.suggestion;
  auto& location = suggestion_with_location.location;
  GenerateObject([&]() {
    GenerateObjectMember("description", suggestion.description(), Position::kFirst);
    std::vector<Replacement> replacements;
    if (suggestion.replacement().has_value()) {
      replacements.emplace_back(Replacement{location, suggestion.replacement().value()});
    }
    GenerateObjectMember("replacements", replacements);
  });
}

void FindingsJson::Generate(const Replacement& replacement) {
  GenerateObject([&]() {
    GenerateObjectMember("replacement", replacement.replacement, Position::kFirst);
    Generate(replacement.location);
  });
}

void FindingsJson::Generate(const SourceLocation& location) {
  GenerateObjectMember("path", location.source_file().filename());

  auto start = location.data();
  auto end = location.data();
  end.remove_prefix(start.size());

  auto end_location = SourceLocation(end, location.source_file());

  SourceFile::Position start_position = location.position();
  SourceFile::Position end_position = end_location.position();

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
