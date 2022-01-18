// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/flat/attributes.h"

#include "fidl/flat/typespace.h"
#include "fidl/flat_ast.h"

namespace fidl::flat {

const AttributeArg* Attribute::GetArg(std::string_view arg_name) const {
  std::string name = utils::canonicalize(arg_name);
  for (const auto& arg : args) {
    if (arg->name.value().data() == name) {
      return arg.get();
    }
  }
  return nullptr;
}

AttributeArg* Attribute::GetStandaloneAnonymousArg() const {
  assert(!compiled &&
         "if calling after attribute compilation, use GetArg(...) with the resolved name instead");
  if (args.size() == 1 && !args[0]->name.has_value()) {
    return args[0].get();
  }
  return nullptr;
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

}  // namespace fidl::flat
