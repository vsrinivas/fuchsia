// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/template_string.h>

#include <iostream>
#include <sstream>

#include <re2/re2.h>

namespace fidl {

std::string TemplateString::Substitute(Substitutions substitutions, bool remove_unmatched) const {
  std::ostringstream os;

  // function-local static pointer to non-trivially-destructible type
  // is allowed by styleguide
  static const auto kRegexReplaceable =
      new re2::RE2(R"(((.?)((?:\$\{([A-Z_][A-Z0-9_]*)\})|(?:\$([A-Z_][A-Z0-9_]*)))))");

  re2::StringPiece full_match;
  re2::StringPiece preceding_char;
  re2::StringPiece var_token;
  re2::StringPiece braced_var;
  re2::StringPiece unbraced_var;

  std::string_view str = str_;
  re2::StringPiece remaining = str;

  while (re2::RE2::FindAndConsume(&remaining, *kRegexReplaceable, &full_match, &preceding_char,
                                  &var_token, &braced_var, &unbraced_var)) {
    auto prefix = str.substr(0, str.length() - full_match.length() - remaining.length());
    os << prefix;
    if (preceding_char == "$") {
      os << full_match.substr(1);  // escaped "$"
    } else {
      if (!preceding_char.empty()) {
        os << preceding_char;
      }
      std::string replaceable =
          !braced_var.empty() ? braced_var.as_string() : unbraced_var.as_string();
      if (substitutions.find(replaceable) != substitutions.end()) {
        os << substitutions[replaceable];
      } else if (!remove_unmatched) {
        os << var_token;
      }
    }
    str = std::string_view(remaining.data(), remaining.length());
  }
  os << str;

  return os.str();
}

}  // namespace fidl
