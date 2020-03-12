// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/parsed_identifier.h"

#include "src/developer/debug/zxdb/expr/expr_parser.h"

namespace zxdb {

std::string ParsedIdentifierComponent::GetName(bool include_debug) const {
  std::string result;

  if (include_debug)
    result.push_back('"');

  if (special_ == SpecialIdentifier::kNone) {
    result += name_;
  } else {
    result.append(SpecialIdentifierToString(special_));
    if (SpecialIdentifierHasData(special_)) {
      result.push_back('(');
      result += name_;
      result.push_back(')');
    }
  }

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

Identifier ToIdentifier(const ParsedIdentifier& parsed) {
  Identifier ret(parsed.qualification());
  ret.components().reserve(parsed.components().size());

  // Just convert each component to its simple name.
  for (const auto& c : parsed.components()) {
    // Assume components either are special or have templates, but not both.
    if (c.special() == SpecialIdentifier::kNone) {
      ret.AppendComponent(IdentifierComponent(c.GetName(false)));
    } else {
      FXL_DCHECK(!c.has_template());
      ret.AppendComponent(IdentifierComponent(c.special(), c.name()));
    }
  }
  return ret;
}

// We want to keep the same component structure regardless of what arbitrary strings were contained
// in the original. So go one component at a time.
ParsedIdentifier ToParsedIdentifier(const Identifier& ident) {
  ParsedIdentifier ret(ident.qualification());
  ret.components().reserve(ident.components().size());

  Err err;
  ParsedIdentifier parsed;
  for (const auto& c : ident.components()) {
    if (c.special() != SpecialIdentifier::kNone) {
      // Assume special components don't have templates and can be copied as-is.
      ret.AppendComponent(ParsedIdentifierComponent(c.special(), c.name()));
      continue;
    }

    // All normal components: try to parse.
    std::string c_name = c.GetName(false);
    err = ExprParser::ParseIdentifier(c_name, &parsed);
    if (err.has_error() || parsed.components().size() != 1) {
      // Give up and use the literal string.
      ret.AppendComponent(ParsedIdentifierComponent(c_name));
    } else {
      ret.AppendComponent(std::move(parsed.components()[0]));
    }
  }
  return ret;
}

}  // namespace zxdb
