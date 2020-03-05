// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fit/function.h"
#include "src/developer/shell/parser/parse_result.h"

#ifndef SRC_DEVELOPER_SHELL_PARSER_TEXT_MATCH_H_
#define SRC_DEVELOPER_SHELL_PARSER_TEXT_MATCH_H_

namespace shell::parser {
namespace internal {

// Produces a parse result for a fixed token or character. This is NOT a parser or combinator; it
// must be told via its arguments whether the initial parse succeeds. The ident argument should
// provide a rendering of what is being matched suitable for error messages. The token matched must
// always be of the given size. The find callback will be used to look ahead for error correction.
// Given a starting offset into the prefix tail, it should return the next location at which a match
// is possible.
template <typename T = ast::Terminal>
ParseResultStream FixedTextResult(ParseResult prefix, const std::string& ident, bool success,
                                  size_t size, fit::function<size_t()> find) {
  if (success) {
    return ParseResultStream(prefix.Advance<T>(size));
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

}  // namespace internal

// Produce a parser which parses any single character from the given list.
fit::function<ParseResultStream(ParseResultStream)> AnyChar(const std::string& name,
                                                            std::string_view chars);

// Produce a parser which parses any single character not in the given list.
fit::function<ParseResultStream(ParseResultStream)> AnyCharBut(const std::string& name,
                                                               std::string_view chars);

// Produce a parser which parses any single character.
ParseResultStream AnyChar(ParseResultStream prefixes);

// Similar to AnyChar but the input string is a regex style range group like "a-zA-Z0-9".
fit::function<ParseResultStream(ParseResultStream)> CharGroup(const std::string& name,
                                                              std::string_view chars);

// Produce a parser to parse a fixed text string.
template <typename T = ast::Terminal>
fit::function<ParseResultStream(ParseResultStream)> Token(const std::string& token) {
  return [token](ParseResultStream prefixes) {
    return std::move(prefixes).Follow([token](ParseResult prefix) {
      return internal::FixedTextResult<T>(
          std::move(prefix), "'" + token + "'", prefix.tail().substr(0, token.size()) == token,
          token.size(), [tail = prefix.tail(), token]() { return tail.find(token); });
    });
  };
}

template <typename T = ast::Terminal>
fit::function<ParseResultStream(ParseResultStream)> Token(
    fit::function<ParseResultStream(ParseResultStream)> parser) {
  return [parser = std::move(parser)](ParseResultStream prefix) {
    return parser(std::move(prefix).Mark()).Reduce<ast::TokenResult>().Map([](ParseResult result) {
      // The goal here is to return a single terminal representing the parsed region of the result,
      // but terminals don't have children, and thus can't have errors. As such, if we have error
      // children, we return an unnamed non-terminal, where we combine all the regular parse results
      // into single tokens, but leave the error tokens in place. So if we parsed ('foo' 'bar'
      // 'baz'), we'd finish with just 'foobarbaz' on the stack, but if we parsed ('foo' 'bar'
      // E[Expected 'baz']) we'd end with ('foobar' E[Expected 'baz']).
      std::optional<size_t> start = std::nullopt;
      size_t end;
      std::vector<std::shared_ptr<ast::Node>> children;
      for (const auto& child : result.node()->Children()) {
        if (child->IsError()) {
          if (start) {
            children.push_back(
                std::make_shared<T>(*start, child->start() - *start,
                                    result.unit().substr(*start, child->start() - *start)));
          }
          children.push_back(child);
          start = std::nullopt;
        } else {
          if (!start) {
            start = child->start();
          }

          end = child->start() + child->Size();
        }
      }

      if (start) {
        size_t size = end - *start;
        children.push_back(std::make_shared<T>(*start, size, result.unit().substr(*start, size)));
      }

      std::shared_ptr<ast::Node> new_child;
      if (children.size() == 1) {
        new_child = children.front();
      } else {
        new_child = std::make_shared<ast::TokenResult>(result.node()->start(), std::move(children));
      }

      return result.SetNode(new_child);
    });
  };
}

}  // namespace shell::parser

#endif  // SRC_DEVELOPER_SHELL_PARSER_TEXT_MATCH_H_
