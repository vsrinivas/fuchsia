// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <src/developer/shell/parser/ast.h>

namespace shell::parser::ast {

const std::vector<std::shared_ptr<Node>> Terminal::kEmptyChildren = {};

std::string Terminal::ToString(std::string_view unit) const {
  std::string prefix;

  return prefix + "'" + std::string(unit.substr(start(), size_)) + "'";
}

std::string Nonterminal::ToString(std::string_view unit) const {
  std::stringstream out;

  out << Name() << "(";

  std::string spacer = "";
  for (const auto& child : Children()) {
    out << spacer << child->ToString(unit);
    spacer = " ";
  }

  out << ")";
  return out.str();
}

std::string Error::ToString(std::string_view unit) const { return "E[" + message_ + "]"; }

std::optional<std::string> Identifier::GetIdentifier(std::string_view unit) const {
  for (const auto& child : Children()) {
    if (child->HasErrors()) {
      continue;
    }

    auto text = child->ToString(unit);
    if (text != "@") {
      return text;
    }
  }

  // TODO: DCHECK(HasErrors()) // We should only get here if the parse failed.
  return std::nullopt;
}

}  // namespace shell::parser::ast
