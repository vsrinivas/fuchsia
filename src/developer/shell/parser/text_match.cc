// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/text_match.h"

namespace shell::parser {
namespace {

// Produces a parse result for a fixed token or character. This is NOT a parser or combinator; it
// must be told via its arguments whether the initial parse succeeds. The ident argument should
// provide a rendering of what is being matched suitable for error messages. The token matched must
// always be of the given size. The find callback will be used to look ahead for error correction.
// Given a starting offset into the prefix tail, it should return the next location at which a match
// is possible.
ParseResultStream FixedTextResult(ParseResult prefix, const std::string& ident, bool success,
                                  size_t size, fit::function<size_t()> find) {
  if (success) {
    return ParseResultStream(prefix.Advance(size));
  }

  return ParseResultStream(false, [prefix, size, ident, find = std::move(find), done = false,
                                   next = std::optional<ParseResult>()]() mutable {
    if (done) {
      return prefix.End();
    }

    if (next) {
      done = true;
      return std::move(*next);
    }

    size_t pos = find();

    ParseResult skip = pos == std::string::npos
                           ? prefix.Skip(prefix.tail().size()).Expected(size, ident)
                           : prefix.Skip(pos).Advance(size);
    ParseResult inject = prefix.Expected(size, ident);

    if (skip.error_score() < inject.error_score()) {
      next = std::move(inject);
      return skip;
    }

    next = std::move(skip);
    return inject;
  });
}

// Produce a parser which parses any single character from the given list. If invert is true,
// character must NOT be in the list instead.
fit::function<ParseResultStream(ParseResultStream)> AnyCharMayInvert(std::string_view chars,
                                                                     bool invert,
                                                                     const std::string& name) {
  return [chars, invert, name](ParseResultStream prefixes) {
    return std::move(prefixes).Follow([chars, invert, name](ParseResult prefix) {
      auto tail = prefix.tail();
      return FixedTextResult(
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
      return FixedTextResult(std::move(prefix), name, matched, 1, [chars, tail]() {
        size_t i = 0;

        while (i < tail.size() && !MatchCharGroup(chars, tail[i])) {
          i++;
        }

        return i;
      });
    });
  };
}

// Produce a parser to parse a fixed text string.
fit::function<ParseResultStream(ParseResultStream)> Token(const std::string& token) {
  return [token](ParseResultStream prefixes) {
    return std::move(prefixes).Follow([token](ParseResult prefix) {
      return FixedTextResult(std::move(prefix), "'" + token + "'",
                             prefix.tail().substr(0, token.size()) == token, token.size(),
                             [tail = prefix.tail(), token]() { return tail.find(token); });
    });
  };
}

}  // namespace shell::parser
