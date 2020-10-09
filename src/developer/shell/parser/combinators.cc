// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/combinators.h"

#include <algorithm>
#include <map>

namespace shell::parser {

fit::function<ParseResult(ParseResult)> Seq(fit::function<ParseResult(ParseResult)> a,
                                            fit::function<ParseResult(ParseResult)> b) {
  return [a = std::move(a), b = std::move(b)](ParseResult prefix) {
    auto a_result = a(prefix);

    if (a_result) {
      return b(a_result);
    }

    return ParseResult::kEnd;
  };
}

fit::function<ParseResult(ParseResult)> Seq(fit::function<ParseResult(ParseResult)> first) {
  return first;
}

fit::function<ParseResult(ParseResult)> Alt(fit::function<ParseResult(ParseResult)> a) { return a; }

fit::function<ParseResult(ParseResult)> Alt(fit::function<ParseResult(ParseResult)> a,
                                            fit::function<ParseResult(ParseResult)> b) {
  return [a = std::move(a), b = std::move(b)](ParseResult prefix) {
    auto a_result = a(prefix);

    if (a_result && a_result.errors() == prefix.errors()) {
      return a_result;
    } else {
      auto b_result = b(prefix);

      if (!b_result) {
        return a_result;
      } else if (!a_result) {
        return b_result;
      } else if (b_result.errors() == prefix.errors()) {
        return b_result;
      } else if (a_result.parsed_successfully() >= b_result.parsed_successfully()) {
        return a_result;
      } else {
        return b_result;
      }
    }
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
    size_t count = 0;
    ParseResult result = prefix;

    for (ParseResult next = ParseResult::kEnd; result && count < max; ++count) {
      next = child(result);

      if ((!next || next.errors() > prefix.errors()) && count >= min) {
        return result;
      }

      // If result becomes a null parse result at this point, it means two things:
      //   1) We've had a hard parse failure. That means parsing failed *and error recovery failed*.
      //   2) We didn't reach the minimum number of repetitions before that hard failure.
      //
      // The next thing that will happen is the loop will end and we will propagate that hard
      // failure by returning the null parse result.
      result = next;
    }

    return result;
  };
}

fit::function<ParseResult(ParseResult)> Multi(size_t count,
                                              fit::function<ParseResult(ParseResult)> child) {
  return Multi(count, count, std::move(child));
}

}  // namespace shell::parser
