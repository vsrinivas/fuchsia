// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/identifier.h"

#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"

namespace zxdb {

std::string Identifier::Component::GetName(bool include_debug) const {
  std::string result;

  if (include_debug)
    result.push_back('"');
  result += name().value();
  if (include_debug)
    result.push_back('"');

  if (has_template()) {
    if (include_debug)
      result.push_back(',');
    result += template_begin().value();

    for (size_t i = 0; i < template_contents().size(); i++) {
      if (i > 0)
        result += ", ";

      // Template parameter string.
      if (include_debug)
        result.push_back('"');
      result += template_contents()[i];
      if (include_debug)
        result.push_back('"');
    }
    result += template_end().value();
  }
  return result;
}

Identifier::Identifier(ExprToken name)
    : Identifier(kRelative, std::move(name)) {}

Identifier::Identifier(Qualification qual, ExprToken name)
    : qualification_(qual) {
  components_.emplace_back(std::move(name));
}

Identifier::Identifier(Component comp)
    : Identifier(kRelative, std::move(comp)) {}

Identifier::Identifier(Qualification qual, Component comp)
    : qualification_(qual) {
  components_.push_back(std::move(comp));
}

// static
std::pair<Err, Identifier> Identifier::FromString(const std::string& input) {
  ExprTokenizer tokenizer(input);
  if (!tokenizer.Tokenize())
    return std::make_pair(tokenizer.err(), Identifier());

  ExprParser parser(tokenizer.TakeTokens());
  auto root = parser.Parse();
  if (!root)
    return std::make_pair(parser.err(), Identifier());

  auto identifier_node = root->AsIdentifier();
  if (!identifier_node) {
    return std::make_pair(Err("Input did not parse as an identifier."),
                          Identifier());
  }

  return std::make_pair(
      Err(),
      const_cast<IdentifierExprNode*>(identifier_node)->TakeIdentifier());
}

void Identifier::AppendComponent(Component c) {
  components_.push_back(std::move(c));
}

void Identifier::AppendComponent(ExprToken name) {
  components_.emplace_back(std::move(name));
}

void Identifier::AppendComponent(ExprToken name, ExprToken template_begin,
                                 std::vector<std::string> template_contents,
                                 ExprToken template_end) {
  components_.emplace_back(std::move(name), std::move(template_begin),
                           std::move(template_contents),
                           std::move(template_end));
}

void Identifier::Append(Identifier other) {
  for (auto& cur : other.components())
    components_.push_back(std::move(cur));
}

Identifier Identifier::GetScope() const {
  if (components_.size() <= 1)
    return Identifier(qualification_);
  return Identifier(qualification_, components_.begin(), components_.end() - 1);
}

std::string Identifier::GetFullName() const { return GetName(false); }

std::string Identifier::GetDebugName() const { return GetName(true); }

// TODO(brettw) remove this function when Identifier can be used everywhere.
std::vector<std::string> Identifier::GetAsIndexComponents() const {
  std::vector<std::string> result;
  result.reserve(components_.size());
  for (const auto& c : components_)
    result.push_back(c.GetName(false));
  return result;
}

const char* Identifier::GetSeparator() const { return "::"; }

const std::string* Identifier::GetSingleComponentName() const {
  if (components_.size() != 1)
    return nullptr;
  if (qualification_ == kGlobal || components_[0].has_template())
    return nullptr;
  return &components_[0].name().value();
}

std::string Identifier::GetName(bool include_debug) const {
  std::string result;

  if (qualification_ == kGlobal)
    result += GetSeparator();

  bool first = true;
  for (const auto& c : components_) {
    if (first) {
      first = false;
    } else {
      if (include_debug)
        result += "; ";
      result += GetSeparator();
    }

    result += c.GetName(include_debug);
  }
  return result;
}

}  // namespace zxdb
