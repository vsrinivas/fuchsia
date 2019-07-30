// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is the filter language parser. Given a filter specification string, the goal is to construct
// an object that represents the filter described by the specification.
//
// The parser is composed of a public interface, described in this file, and internal syntax and
// state implemented in the other parser files. The current design is that the parser is coupled
// with the lexer, as the knowledge about reserved keywords in the filter language must be shared
// between them. However, the parser is agnostic to the concrete form the filter object takes.
// In the classes relating to the parser, the template argument `T` is the type of the filter object
// that will be constructed.
//
// Key to the public parser interface is the `FilterBuilder<T>` class, declared in its own file. Due
// to template classes, the body of parser code is organized as a series of header files.

#ifndef SRC_CONNECTIVITY_NETWORK_NETDUMP_PARSER_H_
#define SRC_CONNECTIVITY_NETWORK_NETDUMP_PARSER_H_

#include <ostream>
#include <sstream>
#include <variant>

#include "parser_internal.h"

namespace netdump::parser {

// ANSI escape sequences used for highlighting.
constexpr auto ANSI_BOLD = "\033[1m";
constexpr auto ANSI_HIGHLIGHT = "\033[1;30;43m";
constexpr auto ANSI_HIGHLIGHT_ERROR = "\033[4;31;43m";
constexpr auto ANSI_RESET = "\033[0m";

using ParseError = std::string;
// Clients access the parsing functionality through this class.
class Parser {
 public:
  // Specifies the `Tokenizer` to use for parsing.
  explicit Parser(const Tokenizer& tkz) : tkz_(tkz) {}

  // Attempt a parse of `filter_spec` and return a filter of type `T` built with `builder`.
  // If the input is successfully parsed, a `T` (variant index 0) is returned.
  // If parsing is unsuccessful, an `Error` (index 1) is returned that explains the syntax error.
  template <class T>
  std::variant<T, ParseError> parse(const std::string& filter_spec, FilterBuilder<T>* builder) {
    Environment env(tkz_.tokenize(filter_spec));

    if (env.at_end()) {
      return error_result<T>("Filter string expected.\n");
    }

    std::optional<T> filter = parse(&env, builder);

    if (filter != std::nullopt) {
      // Parse success, return the filter.
      return filter_result(std::move(*filter));
    }

    // On parse error, build the error message.
    std::string msg = highlight_error(filter_spec, &env);
    return error_result<T>(msg + "\nFilter syntax error: " + env.error_cause + "\n");
  }

  Parser(const Parser&) = delete;
  Parser& operator=(const Parser&) = delete;

 protected:  // Protected instead of private for testing.
  const Tokenizer& tkz_;

  // Allow subclasses to inject a custom `Environment`, e.g. for testing.
  template <class T>
  inline std::optional<T> parse(Environment* env, FilterBuilder<T>* builder) {
    return Syntax<T>(tkz_, env, builder).parse();
  }

  // Insert some ANSI escape characters to highlight the syntax error in console.
  // If the error is at the end location, highlight this by reproducing `filter_spec` and
  // appending an error marker. Otherwise, the the tokens in `env` are reproduced, and the
  // location where the error occurred is highlighted.
  std::string highlight_error(const std::string& filter_spec, Environment* env) {
    if (!env->error_loc.has_value()) {
      return filter_spec;
    }

    std::stringstream msg;
    auto error_loc = *(env->error_loc);
    if (error_loc == env->end()) {
      // The mistake is at the end of the input.
      msg << filter_spec << ANSI_HIGHLIGHT_ERROR << "*" << ANSI_RESET;
      return msg.str();
    }
    // Go to the mistake and highlight it.
    env->reset();
    while (!env->at_end()) {
      if (error_loc == env->cur()) {
        msg << ANSI_HIGHLIGHT_ERROR << (**env)->get_term() << ANSI_RESET;
      } else {
        msg << (**env)->get_term();
      }
      ++(*env);
      if (!env->at_end()) {
        msg << " ";
      }
    }
    return msg.str();
  }

  template <class T>
  inline std::variant<T, ParseError> filter_result(T filter) {
    return std::variant<T, ParseError>(std::in_place_index_t<0>(), std::move(filter));
  }

  template <class T>
  inline std::variant<T, ParseError> error_result(std::string msg) {
    return std::variant<T, ParseError>(std::in_place_index_t<1>(), std::move(msg));
  }
};

// Write a human-readable description of the parser syntax to `output`,
// such as for display on screen. No significant logic should be performed.
void parser_syntax(std::ostream* output);

}  // namespace netdump::parser

#endif  // SRC_CONNECTIVITY_NETWORK_NETDUMP_PARSER_H_
