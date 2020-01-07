// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FINDINGS_JSON_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FINDINGS_JSON_H_

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fidl/source_span.h"
#include "findings.h"
#include "json_writer.h"

namespace fidl {

// |JsonWriter| requires the derived type as a template parameter so it can
// match methods declared with parameter overrides in the derived class.
class FindingsJson : public utils::JsonWriter<FindingsJson> {
 public:
  // "using" is required for overridden methods, so the implementations in
  // both the base class and in this derived class are visible when matching
  // parameter types
  using utils::JsonWriter<FindingsJson>::Generate;
  using utils::JsonWriter<FindingsJson>::GenerateArray;

  // Suggested replacement string and span, per the JSON schema used by
  // Tricium for a findings/diagnostics
  struct Replacement {
    const SourceSpan& span;  // From the Finding
    std::string replacement;
  };

  struct SuggestionWithReplacementSpan {
    const SourceSpan& span;  // From the Finding
    Suggestion suggestion;
  };

  explicit FindingsJson(const Findings& findings) : JsonWriter(json_file_), findings_(findings) {}

  ~FindingsJson() = default;

  std::ostringstream Produce();

  void Generate(const Finding& finding);
  void Generate(const SuggestionWithReplacementSpan& suggestion_with_span);
  void Generate(const Replacement& replacement);
  void Generate(const SourceSpan& span);

 private:
  const Findings& findings_;
  std::ostringstream json_file_;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_FINDINGS_JSON_H_
