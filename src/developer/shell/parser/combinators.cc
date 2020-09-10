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

    if (a_result && a_result.error_score() == 0) {
      return a_result;
    } else {
      auto b_result = b(prefix);

      if (a_result && (!b_result || b_result.error_score() >= a_result.error_score())) {
        return a_result;
      }

      return b_result;
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
    auto inv_result = inv(ParseResult(prefix.tail()));

    if (inv_result && inv_result.error_score() == 0) {
      return ParseResult::kEnd;
    }

    return prefix;
  };
}

fit::function<ParseResult(ParseResult)> Multi(size_t min, size_t max,
                                              fit::function<ParseResult(ParseResult)> child) {
  if (min == 1 && max == 1) {
    return child;
  } else if (min > 0) {
    if (max != std::numeric_limits<size_t>::max()) {
      // TODO: If we can find a way to make the structure of Multi combinators constant-size, we
      // could just treat the max limit like a regular limit and not a sentinel value, which would
      // mean killing this conditional.
      max -= 1;
    }

    // We have to parse at least one instance of the child, so parse it here, then recurse to parse
    // the rest of the pattern.
    return Seq(child.share(), Multi(min - 1, max, std::move(child)));
  } else if (max == std::numeric_limits<size_t>::max()) {
    // This is the same principle as the finite version in the last conditional, but we have to
    // construct the next combinator in a closure otherwise this combinator recurses infinitely.
    return Maybe([child = std::move(child)](ParseResult prefix) mutable {
      auto child_result = child(prefix);

      if (child_result && child_result.error_score() == prefix.error_score()) {
        return Multi(0, std::numeric_limits<size_t>::max(), child.share())(std::move(child_result));
      }

      return child_result;
    });
  } else {
    // Min = 0, max = <something finite>.
    return Maybe(Multi(1, max, std::move(child)));
  }
}

fit::function<ParseResult(ParseResult)> Multi(size_t count,
                                              fit::function<ParseResult(ParseResult)> child) {
  return Multi(count, count, std::move(child));
}

}  // namespace shell::parser
