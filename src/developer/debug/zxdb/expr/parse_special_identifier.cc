// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/parse_special_identifier.h"

#include <ctype.h>

namespace zxdb {

namespace {

bool IsSpecialIdentifierChar(char c) { return isalnum(c) || c == '_'; }

}  // namespace

Err ParseSpecialIdentifier(std::string_view input, size_t* cur, SpecialIdentifier* special,
                           std::string* contents, size_t* error_location) {
  *special = SpecialIdentifier::kNone;
  contents->clear();

  if (*cur >= input.size() || input[*cur] != '$') {
    *error_location = *cur;
    return Err("This is not a special identifier.");  // This is really an internal error.
  }

  // Skip over the '$'.
  size_t special_name_begin = *cur;
  ++(*cur);

  // Extract and move over the special name.
  while (*cur < input.size() && IsSpecialIdentifierChar(input[*cur]))
    ++(*cur);
  std::string_view special_name = input.substr(special_name_begin, *cur - special_name_begin);
  *special = StringToSpecialIdentifier(special_name);
  if (*special == SpecialIdentifier::kNone) {
    *error_location = special_name_begin + 1;  // Text after the "$".
    return Err("The string '" + std::string(special_name) + "' is not a valid special identifier.");
  }

  // A paren following the special name is the contents.
  if (*cur == input.size() || input[*cur] != '(') {
    // No contents. There has to be a special name in this case to prevent us from getting confused
    // by standalone "$".
    if (*special == SpecialIdentifier::kEscaped) {
      *error_location = *cur;
      return Err("Expected special name or '(' for escaped input.");
    }
    return Err();
  }

  // Skip the paren.
  size_t open_paren_index = *cur;
  ++(*cur);

  int paren_depth = 0;

  // Go through the contents of the (). Parens don't need escaping as long as they're matched, but
  // can be escaped with backslashes.
  while (*cur < input.size()) {
    if (input[*cur] == '(') {
      contents->push_back('(');
      paren_depth++;
    } else if (input[*cur] == ')') {
      // Match with other open parens we've encountered, or it indicates the end.
      if (paren_depth == 0) {
        ++(*cur);      // Skip over closing paren.
        return Err();  // Done.
      }
      paren_depth--;
      contents->push_back(')');
    } else if (input[*cur] == '\\') {
      // Backslash escapes.
      ++(*cur);  // Skip over backslash.
      if (*cur == input.size())
        break;  // End-of-loop code handles this case.

      char escaped_char = input[*cur];
      if (escaped_char == '\\' || escaped_char == '(' || escaped_char == ')') {
        // Only allow certain characters to be escaped.
        contents->push_back(escaped_char);
      } else {
        // Everything else is an error.
        *error_location = *cur;
        return Err("Invalid backslash-escaped character in special identifier.");
      }
    } else {
      // All other characters are literals.
      contents->push_back(input[*cur]);
    }

    ++(*cur);
  }

  // Error location indicates the opening paren which makes the error easier to understand.
  *error_location = open_paren_index;
  return Err("Unexpected end of input in special identifier to match.");
}

}  // namespace zxdb
