// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_DIAGNOSTICS_JSON_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_DIAGNOSTICS_JSON_H_

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "fidl/error_types.h"
#include "fidl/source_span.h"
#include "json_writer.h"

namespace fidl {

namespace {

using fidl::errors::BaseError;

enum class DiagnosticType {
  kError,
  kWarning,
};

struct Diagnostic {
  DiagnosticType type;
  BaseError* error_or_warning;
};

}  // namespace

// |JsonWriter| requires the derived type as a template parameter so it can
// match methods declared with parameter overrides in the derived class.
class DiagnosticsJson : public utils::JsonWriter<DiagnosticsJson> {
 public:
  // "using" is required for overridden methods, so the implementations in
  // both the base class and in this derived class are visible when matching
  // parameter types
  using utils::JsonWriter<DiagnosticsJson>::Generate;
  using utils::JsonWriter<DiagnosticsJson>::GenerateArray;

  explicit DiagnosticsJson(const std::vector<std::unique_ptr<BaseError>>& errors,
                           const std::vector<std::unique_ptr<BaseError>>& warnings)
      : JsonWriter(json_file_), errors_(errors), warnings_(warnings) {}

  ~DiagnosticsJson() = default;

  std::ostringstream Produce();

  void Generate(const Diagnostic& diagnostic);
  void Generate(const SourceSpan& span);

 private:
  const std::vector<std::unique_ptr<BaseError>>& errors_;
  const std::vector<std::unique_ptr<BaseError>>& warnings_;
  std::ostringstream json_file_;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_DIAGNOSTICS_JSON_H_
