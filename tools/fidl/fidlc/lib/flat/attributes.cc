// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/flat/attributes.h"

#include <zircon/assert.h>

#include "tools/fidl/fidlc/include/fidl/flat/typespace.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/include/fidl/utils.h"

namespace fidl::flat {

std::unique_ptr<AttributeArg> AttributeArg::Clone() const {
  return std::make_unique<AttributeArg>(name, value->Clone(), span);
}

const AttributeArg* Attribute::GetArg(std::string_view arg_name) const {
  std::string name = utils::canonicalize(arg_name);
  for (const auto& arg : args) {
    if (arg->name && arg->name.value().data() == name) {
      return arg.get();
    }
  }
  return nullptr;
}

AttributeArg* Attribute::GetStandaloneAnonymousArg() const {
  ZX_ASSERT_MSG(
      !compiled,
      "if calling after attribute compilation, use GetArg(...) with the resolved name instead");
  if (args.size() == 1 && !args[0]->name.has_value()) {
    return args[0].get();
  }
  return nullptr;
}

std::unique_ptr<Attribute> Attribute::Clone() const {
  auto attribute = std::make_unique<Attribute>(name, utils::MapClone(args), span);
  attribute->compiled = compiled;
  return attribute;
}

const Attribute* AttributeList::Get(std::string_view attribute_name) const {
  for (const auto& attribute : attributes) {
    if (attribute->name.data() == attribute_name)
      return attribute.get();
  }
  return nullptr;
}

Attribute* AttributeList::Get(std::string_view attribute_name) {
  for (const auto& attribute : attributes) {
    if (attribute->name.data() == attribute_name)
      return attribute.get();
  }
  return nullptr;
}

std::unique_ptr<AttributeList> AttributeList::Clone() const {
  return std::make_unique<AttributeList>(utils::MapClone(attributes));
}

}  // namespace fidl::flat
