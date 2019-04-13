// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/identifier.h"

namespace zxdb {

std::string Identifier::Component::GetName(bool include_debug) const {
  std::string result;

  if (include_debug)
    result.push_back('"');
  result += name_;
  if (include_debug)
    result.push_back('"');

  if (has_template()) {
    if (include_debug)
      result.push_back(',');
    result.push_back('<');

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
    result.push_back('>');
  }
  return result;
}

Identifier::Identifier(std::string name)
    : Identifier(kRelative, std::move(name)) {}

Identifier::Identifier(Qualification qual, std::string name)
    : qualification_(qual) {
  components_.emplace_back(std::move(name));
}

Identifier::Identifier(Component comp)
    : Identifier(kRelative, std::move(comp)) {}

Identifier::Identifier(Qualification qual, Component comp)
    : qualification_(qual) {
  components_.push_back(std::move(comp));
}

void Identifier::AppendComponent(Component c) {
  components_.push_back(std::move(c));
}

void Identifier::AppendComponent(std::string name) {
  components_.emplace_back(std::move(name));
}

void Identifier::AppendComponent(std::string name,
                                 std::vector<std::string> template_contents) {
  components_.emplace_back(std::move(name), std::move(template_contents));
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
  return &components_[0].name();
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
