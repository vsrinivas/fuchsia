// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/combinators.h"

#include <algorithm>
#include <map>

namespace shell::parser {

fit::function<ParseResultStream(ParseResultStream)> Seq(
    fit::function<ParseResultStream(ParseResultStream)> a,
    fit::function<ParseResultStream(ParseResultStream)> b) {
  return [a = std::move(a), b = std::move(b)](ParseResultStream prefixes) mutable {
    auto a_result = a(std::move(prefixes));

    if (!a_result.ok()) {
      // If `a` fails, we don't even want to bother parsing `b` unless we end up in an
      // error-correction path, so return a parse result stream that will lazily attempt the parse
      // of `b` if parse results are requested.
      return ParseResultStream(false, [a_result = std::move(a_result), b = b.share(),
                                       b_result = std::optional<ParseResultStream>()]() mutable {
        if (!b_result) {
          b_result = b(std::move(a_result));
        }

        return b_result->Next();
      });
    } else {
      return b(std::move(a_result));
    }
  };
}

fit::function<ParseResultStream(ParseResultStream)> Seq(
    fit::function<ParseResultStream(ParseResultStream)> first) {
  return first;
}

fit::function<ParseResultStream(ParseResultStream)> Alt(
    fit::function<ParseResultStream(ParseResultStream)> a) {
  return a;
}

fit::function<ParseResultStream(ParseResultStream)> Alt(
    fit::function<ParseResultStream(ParseResultStream)> a,
    fit::function<ParseResultStream(ParseResultStream)> b) {
  return [a = std::move(a), b = std::move(b)](ParseResultStream prefixes) mutable {
    auto [a_prefixes, b_prefixes] = std::move(prefixes).Fork();
    auto a_result = a(std::move(a_prefixes));

    if (a_result.ok()) {
      return a_result;
    } else {
      return b(std::move(b_prefixes));
    }
  };
}

ParseResultStream Empty(ParseResultStream prefixes) { return prefixes; }

ParseResultStream EOS(ParseResultStream prefixes) {
  return std::move(prefixes).Follow([](ParseResult result) {
    if (result.tail().empty()) {
      return ParseResultStream(result);
    } else {
      return ParseResultStream(result.Skip(result.tail().size())).Fail();
    }
  });
}

fit::function<ParseResultStream(ParseResultStream)> Not(
    fit::function<ParseResultStream(ParseResultStream)> inv) {
  return [inv = std::move(inv)](ParseResultStream prefixes) mutable {
    return std::move(prefixes).Follow([inv = inv.share()](ParseResult result) {
      auto inv_parse = inv(ParseResultStream(result));

      if (!inv_parse.ok()) {
        return ParseResultStream(result);
      } else {
        auto got = inv_parse.Next().node();
        auto error =
            "Ambiguous sequence: '" + std::string(result.tail().substr(0, got->Size())) + "'";
        auto error_node = std::make_unique<ast::Error>(result.offset(), 0, error);
        return ParseResultStream(result.InjectError(got->Size(), std::move(error_node))).Fail();
      }
    });
  };
}

fit::function<ParseResultStream(ParseResultStream)> Multi(
    size_t min, size_t max, fit::function<ParseResultStream(ParseResultStream)> child) {
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
    return Maybe([child = std::move(child)](ParseResultStream prefixes) mutable {
      return Multi(1, std::numeric_limits<size_t>::max(), child.share())(std::move(prefixes));
    });
  } else if (max == 1) {
    // Multi(0, 1, ...) is just Maybe(...)
    return Maybe(std::move(child));
  } else {
    // Min = 0, max = <something finite>. We can wrap this in a
    return Maybe(Multi(1, max, std::move(child)));
  }
}

fit::function<ParseResultStream(ParseResultStream)> Multi(
    size_t count, fit::function<ParseResultStream(ParseResultStream)> child) {
  return Multi(count, count, std::move(child));
}

}  // namespace shell::parser
