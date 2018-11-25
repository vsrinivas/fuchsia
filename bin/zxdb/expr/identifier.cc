// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/identifier.h"

namespace zxdb {

Identifier::Identifier(ExprToken name) {
  components_.emplace_back(ExprToken(), std::move(name));
}

void Identifier::AppendComponent(Component c) {
  components_.push_back(std::move(c));
}

void Identifier::AppendComponent(ExprToken separator, ExprToken name) {
  components_.emplace_back(std::move(separator), std::move(name));
}

void Identifier::AppendComponent(ExprToken separator, ExprToken name,
                                 ExprToken template_begin,
                                 std::vector<std::string> template_contents,
                                 ExprToken template_end) {
  components_.emplace_back(
      std::move(separator), std::move(name), std::move(template_begin),
      std::move(template_contents), std::move(template_end));
}

std::string Identifier::GetFullName() const {
  return GetName(false);
}

std::string Identifier::GetDebugName() const {
  return GetName(true);
}

std::string Identifier::GetName(bool include_debug) const {
  std::string result;
  bool first = true;
  for (const auto& c : components_) {
    if (first)
      first = false;
    else if (include_debug)
      result += "; ";

    if (c.has_separator()) {
      result += c.separator().value();
      if (include_debug)
        result.push_back(',');
    }

    if (include_debug)
      result.push_back('"');
    result += c.name().value();
    if (include_debug)
      result.push_back('"');

    if (c.has_template()) {
      if (include_debug)
        result.push_back(',');
      result += c.template_begin().value();

      for (size_t i = 0; i < c.template_contents().size(); i++) {
        if (i > 0)
          result += ", ";

        // Template parameter string.
        if (include_debug)
          result.push_back('"');
        result += c.template_contents()[i];
        if (include_debug)
          result.push_back('"');
      }
      result += c.template_end().value();
    }
  }
  return result;
}

}  // namespace zxdb
