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
        Generate(finding.source_location());
        std::vector<SuggestionWithReplacementLocation> suggestions;
        if (finding.suggestion().has_value()) {
            suggestions.emplace_back(SuggestionWithReplacementLocation{
                finding.source_location(),
                finding.suggestion().value()});
        }
        GenerateObjectMember("suggestions", suggestions);
    });
}

void FindingsJson::Generate(const SuggestionWithReplacementLocation& suggestion_with_location) {
    auto& suggestion = suggestion_with_location.suggestion;
    auto& source_location = suggestion_with_location.source_location;
    GenerateObject([&]() {
        GenerateObjectMember("description", suggestion.description(), Position::kFirst);
        std::vector<Replacement> replacements;
        if (suggestion.replacement().has_value()) {
            replacements.emplace_back(
                Replacement{source_location, suggestion.replacement().value()});
        }
        GenerateObjectMember("replacements", replacements);
    });
}

void FindingsJson::Generate(const Replacement& replacement) {
    GenerateObject([&]() {
        GenerateObjectMember("replacement", replacement.replacement, Position::kFirst);
        Generate(replacement.source_location);
    });
}

void FindingsJson::Generate(const SourceLocation& source_location) {
    GenerateObjectMember("path", source_location.source_file().filename());

    auto start = source_location.data();
    auto end = source_location.data();
    end.remove_prefix(start.size());

    auto end_location = SourceLocation(end, source_location.source_file());

    SourceFile::Position start_position = source_location.position();
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

} // namespace fidl
