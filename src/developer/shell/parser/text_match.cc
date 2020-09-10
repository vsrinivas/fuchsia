// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/text_match.h"

namespace shell::parser {
namespace {

// Produce a parser which parses any single character from the given list. If invert is true,
// character must NOT be in the list instead.
fit::function<ParseResult(ParseResult)> AnyCharMayInvert(std::string_view chars, bool invert) {
  return [chars, invert](ParseResult prefix) {
    if (prefix.tail().size() > 0 && (chars.find(prefix.tail()[0]) == std::string::npos) == invert) {
      return prefix.Advance(1);
    }

    return ParseResult::kEnd;
  };
}

// Return whether a given character would match a regex-style char group.
bool MatchCharGroup(std::string_view chars, char c) {
  size_t pos = 0;

  while (pos < chars.size()) {
    if (c == chars[pos]) {
      return true;
    } else if (pos + 2 < chars.size() && chars[pos + 1] == '-') {
      if (c > chars[pos] && c <= chars[pos + 2]) {
        return true;
      }

      pos += 3;
    } else {
      pos += 1;
    }
  }

  return false;
}

}  // namespace

fit::function<ParseResult(ParseResult)> AnyChar(std::string_view chars) {
  return AnyCharMayInvert(chars, false);
}

ParseResult AnyChar(ParseResult prefix) { return AnyCharBut("")(std::move(prefix)); }

fit::function<ParseResult(ParseResult)> AnyCharBut(std::string_view chars) {
  return AnyCharMayInvert(chars, true);
}

fit::function<ParseResult(ParseResult)> CharGroup(std::string_view chars) {
  return [chars](ParseResult prefix) {
    if (prefix.tail().size() > 0 && MatchCharGroup(chars, prefix.tail()[0])) {
      return prefix.Advance(1);
    }

    return ParseResult::kEnd;
  };
}

}  // namespace shell::parser
