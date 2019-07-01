// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FINDINGS_JSON_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FINDINGS_JSON_H_

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fidl/source_location.h"
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

  // Suggested replacement string and location, per the JSON schema used by
  // Tricium for a findings/diagnostics
  struct Replacement {
    const SourceLocation& location;  // From the Finding
    std::string replacement;
  };

  struct SuggestionWithReplacementLocation {
    const SourceLocation& location;  // From the Finding
    Suggestion suggestion;
  };

  explicit FindingsJson(const Findings& findings)
      : JsonWriter(json_file_), findings_(findings) {}

  ~FindingsJson() = default;

  std::ostringstream Produce();

  void Generate(const Finding& value);
  void Generate(const SuggestionWithReplacementLocation& value);
  void Generate(const Replacement& value);
  void Generate(const SourceLocation& value);

 private:
  const Findings& findings_;
  std::ostringstream json_file_;
};

}  // namespace fidl

#endif  // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_FINDINGS_JSON_H_
