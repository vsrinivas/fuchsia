// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/text_match.h"

namespace shell::parser {
namespace internal {}  // namespace internal

namespace {

// Produce a parser which parses any single character from the given list. If invert is true,
// character must NOT be in the list instead.
fit::function<ParseResultStream(ParseResultStream)> AnyCharMayInvert(std::string_view chars,
                                                                     bool invert,
                                                                     const std::string& name) {
  return [chars, invert, name](ParseResultStream prefixes) {
    return std::move(prefixes).Follow([chars, invert, name](ParseResult prefix) {
      auto tail = prefix.tail();
      return internal::FixedTextResult(
          std::move(prefix), name,
          tail.size() > 0 && (chars.find(tail[0]) == std::string::npos) == invert, 1,
          [chars, invert, tail]() {
            if (invert) {
              return tail.find_first_not_of(chars);
            } else {
              return tail.find_first_of(chars);
            }
          });
    });
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

fit::function<ParseResultStream(ParseResultStream)> AnyChar(const std::string& name,
                                                            std::string_view chars) {
  return AnyCharMayInvert(chars, false, name);
}

ParseResultStream AnyChar(ParseResultStream prefixes) {
  return AnyCharBut("a character", "")(std::move(prefixes));
}

fit::function<ParseResultStream(ParseResultStream)> AnyCharBut(const std::string& name,
                                                               std::string_view chars) {
  return AnyCharMayInvert(chars, true, name);
}

fit::function<ParseResultStream(ParseResultStream)> CharGroup(const std::string& name,
                                                              std::string_view chars) {
  return [chars, name](ParseResultStream prefixes) {
    return std::move(prefixes).Follow([chars, name](ParseResult prefix) {
      auto tail = prefix.tail();
      bool matched = tail.size() > 0 && MatchCharGroup(chars, tail[0]);
      return internal::FixedTextResult(std::move(prefix), name, matched, 1, [chars, tail]() {
        size_t i = 0;

        while (i < tail.size() && !MatchCharGroup(chars, tail[i])) {
          i++;
        }

        return i;
      });
    });
  };
}

}  // namespace shell::parser
