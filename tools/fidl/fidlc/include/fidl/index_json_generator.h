// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_INDEX_JSON_GENERATOR_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_INDEX_JSON_GENERATOR_H_

#include <zircon/assert.h>

#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "experimental_flags.h"
#include "fidl/names.h"
#include "flat/compiler.h"
#include "flat_ast.h"
#include "json_writer.h"

namespace fidl {

class IndexJSONGenerator : public utils::JsonWriter<IndexJSONGenerator> {
 public:
  // "using" is required for overridden methods, so the implementations in
  // both the base class and in this derived class are visible when matching
  // parameter types
  using utils::JsonWriter<IndexJSONGenerator>::Generate;
  using utils::JsonWriter<IndexJSONGenerator>::GenerateArray;

  explicit IndexJSONGenerator(const flat::Compilation* compilation)
      : JsonWriter(json_file_), compilation_(compilation) {}

  ~IndexJSONGenerator() = default;

  // struct representing an identifier from dependency library referenced in target library
  struct ReferencedIdentifier {
    explicit ReferencedIdentifier(const flat::Name& name) : identifier(NameFlatName(name)) {
      ZX_ASSERT_MSG(name.span().has_value(), "anonymous name used as an identifier");
      span = name.span().value();
    }
    ReferencedIdentifier(std::string identifier, SourceSpan span)
        : span(span), identifier(std::move(identifier)) {}
    SourceSpan span;
    std::string identifier;
  };

  void Generate(SourceSpan value);
  void Generate(ReferencedIdentifier value);
  void Generate(long value) { EmitNumeric(value); }
  void Generate(unsigned long value) { EmitNumeric(value); }
  void Generate(const flat::Compilation::Dependency& dependency);
  void Generate(std::pair<flat::Library*, SourceSpan> reference);
  void Generate(const flat::Const& value);
  void Generate(const flat::Constant& value);
  void Generate(const flat::Enum& value);
  void Generate(const flat::Enum::Member& value);
  void Generate(const flat::Name& value);
  void Generate(const flat::Struct& value);
  void Generate(const flat::Struct::Member& value);
  void Generate(const flat::TypeConstructor* value);
  void Generate(const flat::Protocol& value);
  void Generate(const flat::Protocol::ComposedProtocol& value);
  void Generate(const flat::Protocol::MethodWithInfo& method_with_info);

  std::ostringstream Produce();

 private:
  std::vector<ReferencedIdentifier> GetDependencyIdentifiers();
  const flat::Compilation* compilation_;
  std::ostringstream json_file_;
};
}  // namespace fidl

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_INDEX_JSON_GENERATOR_H_
