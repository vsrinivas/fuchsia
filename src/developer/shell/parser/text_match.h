// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fit/function.h"
#include "src/developer/shell/parser/parse_result.h"

#ifndef SRC_DEVELOPER_SHELL_PARSER_TEXT_MATCH_H_
#define SRC_DEVELOPER_SHELL_PARSER_TEXT_MATCH_H_

namespace shell::parser {

// Produce a parser which parses any single character from the given list.
fit::function<ParseResult(ParseResult)> AnyChar(std::string_view chars);

// Produce a parser which parses any single character not in the given list.
fit::function<ParseResult(ParseResult)> AnyCharBut(std::string_view chars);

// Produce a parser which parses any single character.
ParseResult AnyChar(ParseResult prefix);

// Similar to AnyChar but the input string is a regex style range group like "a-zA-Z0-9".
fit::function<ParseResult(ParseResult)> CharGroup(std::string_view chars);

// Produce a parser to parse a fixed text string.
template <typename T = ast::Terminal>
fit::function<ParseResult(ParseResult)> Token(const std::string& token) {
  return [token](ParseResult prefix) {
    if (prefix.tail().substr(0, token.size()) == token) {
      return prefix.Advance<T>(token.size());
    }

    return ParseResult::kEnd;
  };
}

template <typename T = ast::Terminal>
fit::function<ParseResult(ParseResult)> Token(fit::function<ParseResult(ParseResult)> parser) {
  return [parser = std::move(parser)](ParseResult prefix) {
    auto result = parser(std::move(prefix).Mark()).Reduce<ast::TokenResult>();

    if (!result) {
      return ParseResult::kEnd;
    }

    return result.MapNode(
        [unit = result.unit()](std::shared_ptr<ast::Node> node) -> std::shared_ptr<ast::Node> {
          // The goal here is to return a single terminal representing the parsed region of the
          // result, but terminals don't have children, and thus can't have errors. As such, if we
          // have error children, we return an unnamed non-terminal, where we combine all the
          // regular parse results into single tokens, but leave the error tokens in place. So if we
          // parsed ('foo' 'bar' 'baz'), we'd finish with just 'foobarbaz' on the stack, but if we
          // parsed ('foo' 'bar' E[Expected 'baz']) we'd end with ('foobar' E[Expected 'baz']).
          std::optional<size_t> start = std::nullopt;
          size_t end;
          std::vector<std::shared_ptr<ast::Node>> children;
          for (const auto& child : node->Children()) {
            if (child->IsError()) {
              if (start) {
                children.push_back(std::make_shared<T>(
                    *start, child->start() - *start, unit.substr(*start, child->start() - *start)));
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
            children.push_back(std::make_shared<T>(*start, size, unit.substr(*start, size)));
          }

          if (children.size() == 1) {
            return children.front();
          } else {
            return std::make_shared<ast::TokenResult>(node->start(), std::move(children));
          }
        });
  };
}

}  // namespace shell::parser

#endif  // SRC_DEVELOPER_SHELL_PARSER_TEXT_MATCH_H_
