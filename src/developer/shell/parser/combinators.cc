// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/combinators.h"

#include <algorithm>
#include <map>

namespace shell::parser {
namespace {

ParseResult Follow(ParseResult a_result, const fit::function<ParseResult(ParseResult)>& b) {
  if (!a_result) {
    return ParseResult::kEnd;
  }

  auto b_result = b(a_result);

  if (b_result && (b_result.errors() == a_result.errors() &&
                   b_result.parsed_successfully() > a_result.parsed_successfully())) {
    return b_result;
  }

  if (auto alt = a_result.error_alternative()) {
    auto b_alt_result = b(*alt);

    if (b_alt_result &&
        (!b_result || b_alt_result.parsed_successfully() > b_result.parsed_successfully())) {
      return b_alt_result;
    }
  }

  return b_result;
}

}  // namespace

fit::function<ParseResult(ParseResult)> Seq(fit::function<ParseResult(ParseResult)> a,
                                            fit::function<ParseResult(ParseResult)> b) {
  return [a = std::move(a), b = std::move(b)](ParseResult prefix) { return Follow(a(prefix), b); };
}

fit::function<ParseResult(ParseResult)> Seq(fit::function<ParseResult(ParseResult)> first) {
  return first;
}

fit::function<ParseResult(ParseResult)> Alt(fit::function<ParseResult(ParseResult)> a) { return a; }

fit::function<ParseResult(ParseResult)> Alt(fit::function<ParseResult(ParseResult)> a,
                                            fit::function<ParseResult(ParseResult)> b) {
  return [a = std::move(a), b = std::move(b)](ParseResult prefix) {
    auto a_result = a(prefix);

    if (!a_result) {
      return b(prefix);
    }

    if (a_result.errors() != prefix.errors()) {
      if (auto b_result = b(prefix)) {
        if (a_result.parsed_successfully() < b_result.parsed_successfully()) {
          return b_result;
        }

        if (b_result.errors() == prefix.errors()) {
          return b_result.WithAlternative(a_result);
        }
      }
    }

    return a_result;
  };
}

ParseResult Empty(ParseResult prefix) { return prefix; }

ParseResult EOS(ParseResult prefix) {
  if (prefix.tail().empty()) {
    return prefix;
  }

  return ParseResult::kEnd;
}

fit::function<ParseResult(ParseResult)> Not(fit::function<ParseResult(ParseResult)> inv) {
  return [inv = std::move(inv)](ParseResult prefix) {
    auto inv_result = inv(prefix);

    if (inv_result && inv_result.errors() == prefix.errors()) {
      return ParseResult::kEnd;
    }

    return prefix;
  };
}

fit::function<ParseResult(ParseResult)> Multi(size_t min, size_t max,
                                              fit::function<ParseResult(ParseResult)> child) {
  return [child = std::move(child), min, max](ParseResult prefix) {
    ParseResult result = min == 0 ? prefix : ParseResult::kEnd;
    ParseResult furthest = prefix;

    size_t count = 0;
    while (count < max) {
      ParseResult next = count == 0 ? child(furthest) : Follow(furthest, child);

      if (!next) {
        break;
      }

      if (next.parsed_successfully() <= furthest.parsed_successfully()) {
        break;
      }

      furthest = next;
      count++;

      if (count == min || (count > min && furthest.errors() == prefix.errors())) {
        result = furthest;
      }
    }

    if (!result) {
      if (furthest.errors() > prefix.errors()) {
        return furthest;
      }

      return ParseResult::kEnd;
    }

    if (result.errors() == prefix.errors() &&
        furthest.parsed_successfully() > result.parsed_successfully()) {
      return result.WithAlternative(furthest);
    }

    return result;
  };
}

fit::function<ParseResult(ParseResult)> Multi(size_t count,
                                              fit::function<ParseResult(ParseResult)> child) {
  return Multi(count, count, std::move(child));
}

}  // namespace shell::parser
