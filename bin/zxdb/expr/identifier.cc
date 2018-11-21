// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/identifier.h"

namespace zxdb {

Identifier::Identifier(ExprToken name) {
  components_.emplace_back(ExprToken(), std::move(name), ExprToken());
}

void Identifier::AppendComponent(Component c) {
  components_.push_back(std::move(c));
}

void Identifier::AppendComponent(ExprToken separator, ExprToken name,
                                 ExprToken template_spec) {
  components_.emplace_back(std::move(separator), std::move(name),
                           std::move(template_spec));
}

std::string Identifier::GetFullName() const {
  std::string result;
  for (const auto& c : components_) {
    if (c.has_separator())
      result += c.separator().value();
    result += c.name().value();
    if (c.has_template_spec())
      result += c.template_spec().value();
  }
  return result;
}

std::string Identifier::GetDebugName() const {
  std::string result;
  bool first = true;
  for (const auto& c : components_) {
    if (first)
      first = false;
    else
      result += "; ";

    if (c.has_separator()) {
      result += c.separator().value();
      result.push_back(',');
    }

    result.push_back('"');
    result += c.name().value();
    result.push_back('"');

    if (c.has_template_spec()) {
      result += ",\"";
      result += c.template_spec().value();
      result.push_back('"');
    }
  }
  return result;
}

}  // namespace zxdb
