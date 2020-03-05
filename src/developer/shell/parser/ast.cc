// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/ast.h"

#include <limits>
#include <sstream>

#include "src/lib/syslog/cpp/logger.h"

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

DecimalGroup::DecimalGroup(size_t start, size_t size, std::string_view content)
    : Terminal(start, size, content), digits_(content.size()) {
  digits_ = content.size();

  for (const char& ch : content) {
    uint64_t old_value = value_;
    value_ *= 10;
    FX_DCHECK(value_ >= old_value) << "Insufficient precision to store DecimalGroup value.";
    value_ += ch - '0';
  }
}

HexGroup::HexGroup(size_t start, size_t size, std::string_view content)
    : Terminal(start, size, content), digits_(content.size()) {
  digits_ = content.size();

  FX_DCHECK(digits_ <= 16) << "Insufficient precision to store HexGroup value.";

  for (const char& ch : content) {
    value_ *= 16;

    if (ch - '0' <= 9) {
      value_ += ch - '0';
    } else if (ch - 'A' <= 5) {
      value_ += ch - 'A' + 10;
    } else {
      value_ += ch - 'a' + 10;
    }
  }
}

Integer::Integer(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  for (const auto& child : Children()) {
    if (auto hex = child->AsHexGroup()) {
      auto next = value_ << hex->digits() * 4;
      FX_DCHECK(next >= value_) << "Insufficient precision to store Integer value.";
      value_ = next + hex->value();
    } else if (auto dec = child->AsDecimalGroup()) {
      auto old_value = value_;
      for (size_t i = 0; i < dec->digits(); i++, value_ *= 10)
        ;
      FX_DCHECK(value_ >= old_value) << "Insufficient precision to store Integer value.";
      value_ += dec->value();
    }
  }
}

VariableDecl::VariableDecl(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  for (const auto& child : Children()) {
    if (child->IsConst()) {
      is_const_ = true;
    } else if (auto expr = child->AsExpression()) {
      expression_ = expr;
    } else if (auto id = child->AsIdentifier()) {
      identifier_ = id->identifier();
    }
  }
}

Identifier::Identifier(size_t start, std::vector<std::shared_ptr<Node>> children)
    : Nonterminal(start, std::move(children)) {
  for (const auto& child : Children()) {
    if (auto ue = child->AsUnescapedIdentifier()) {
      identifier_ = ue->identifier();
      break;
    }
  }
}

}  // namespace shell::parser::ast
