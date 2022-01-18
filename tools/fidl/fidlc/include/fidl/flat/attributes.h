// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_ATTRIBUTES_H_
#define TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_ATTRIBUTES_H_

#include <lib/fit/function.h>

#include <memory>
#include <optional>

#include "fidl/flat/values.h"
#include "fidl/source_span.h"

namespace fidl::flat {

struct AttributeArg final {
  AttributeArg(std::optional<SourceSpan> name, std::unique_ptr<Constant> value, SourceSpan span)
      : name(name), value(std::move(value)), span(span) {}

  // Span of just the argument name, e.g. "bar". This is initially null for
  // arguments like `@foo("abc")`, but will be set during compilation.
  std::optional<SourceSpan> name;
  std::unique_ptr<Constant> value;
  // Span of the entire argument, e.g. `bar="abc"`, or `"abc"` if unnamed.
  const SourceSpan span;

  // Default name to use for arguments like `@foo("abc")` when there is no
  // schema for `@foo` we can use to infer the name.
  static constexpr std::string_view kDefaultAnonymousName = "value";
};

struct Attribute final {
  // A constructor for synthetic attributes like @result.
  explicit Attribute(SourceSpan name) : name(name) {}

  Attribute(SourceSpan name, std::vector<std::unique_ptr<AttributeArg>> args, SourceSpan span)
      : name(name), args(std::move(args)), span(span) {}

  const AttributeArg* GetArg(std::string_view arg_name) const;

  // Returns the lone argument if there is exactly 1 and it is not named. For
  // example it returns non-null for `@foo("x")` but not for `@foo(bar="x")`.
  AttributeArg* GetStandaloneAnonymousArg() const;

  // Span of just the attribute name not including the "@", e.g. "foo".
  const SourceSpan name;
  const std::vector<std::unique_ptr<AttributeArg>> args;
  // Span of the entire attribute, e.g. `@foo(bar="abc")`.
  const SourceSpan span;
  // Set to true by Library::CompileAttribute.
  bool compiled = false;

  // We parse `///` doc comments as nameless raw::Attribute with `provenance`
  // set to raw::Attribute::Provenance::kDocComment. When consuming into a
  // flat::Attribute, we set the name to kDocCommentName.
  static constexpr std::string_view kDocCommentName = "doc";
};

// In the flat AST, "no attributes" is represented by an AttributeList
// containing an empty vector. (In the raw AST, null is used instead.)
struct AttributeList final {
  explicit AttributeList() = default;
  explicit AttributeList(std::vector<std::unique_ptr<Attribute>> attributes)
      : attributes(std::move(attributes)) {}

  bool Empty() const { return attributes.empty(); }
  const Attribute* Get(std::string_view attribute_name) const;
  Attribute* Get(std::string_view attribute_name);

  std::vector<std::unique_ptr<Attribute>> attributes;
};

}  // namespace fidl::flat

#endif  // TOOLS_FIDL_FIDLC_INCLUDE_FIDL_FLAT_ATTRIBUTES_H_
