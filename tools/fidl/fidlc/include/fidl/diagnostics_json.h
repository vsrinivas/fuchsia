// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_JSON_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_JSON_H_

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "tools/fidl/fidlc/include/fidl/diagnostic_types.h"
#include "tools/fidl/fidlc/include/fidl/json_writer.h"
#include "tools/fidl/fidlc/include/fidl/source_span.h"

namespace fidl {

// |JsonWriter| requires the derived type as a template parameter so it can
// match methods declared with parameter overrides in the derived class.
//
// Specification of the output format is
// https://chromium.googlesource.com/infra/infra/+/refs/heads/master/go/src/infra/tricium/api/v1/data.proto#135
class DiagnosticsJson : public utils::JsonWriter<DiagnosticsJson> {
 public:
  // "using" is required for overridden methods, so the implementations in
  // both the base class and in this derived class are visible when matching
  // parameter types
  using utils::JsonWriter<DiagnosticsJson>::Generate;
  using utils::JsonWriter<DiagnosticsJson>::GenerateArray;

  explicit DiagnosticsJson(std::vector<Diagnostic*> diagnostics)
      : JsonWriter(json_file_), diagnostics_(std::move(diagnostics)) {}

  ~DiagnosticsJson() = default;

  std::ostringstream Produce();

  void Generate(const Diagnostic* diagnostic);
  void Generate(const SourceSpan& span);

 private:
  std::vector<Diagnostic*> diagnostics_;
  std::ostringstream json_file_;
};

}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_DIAGNOSTICS_JSON_H_
