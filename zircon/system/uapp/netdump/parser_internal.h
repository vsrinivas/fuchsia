// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file implements the syntax logic for the packet filter language.

#ifndef ZIRCON_SYSTEM_UAPP_NETDUMP_PARSER_INTERNAL_H_
#define ZIRCON_SYSTEM_UAPP_NETDUMP_PARSER_INTERNAL_H_

#include <arpa/inet.h>
#include <zircon/assert.h>

#include <sstream>

#include "filter_builder.h"
#include "parser_state.h"

namespace netdump::parser {

constexpr auto ERROR_EXPECTED_LENGTH = "Length value expected.";
constexpr auto ERROR_INVALID_LENGTH = "Invalid length value.";
constexpr auto ERROR_REQUIRED_CONNECTIVE = "Logical connective required.";
constexpr auto ERROR_UNEXPECTED_R_PARENS = "Unexpected ')'.";
constexpr auto ERROR_UNEXPECTED_CONNECTIVE = "Unexpected logical connective.";
constexpr auto ERROR_UNKNOWN_KEYWORD = "Unknown keyword.";
constexpr auto ERROR_UNMATCHED_L_PARENS = "Parenthesis without matching ')'.";

template <class T>
class Syntax {
#define TOKEN (**env_)
#define ENV (*env_)
 public:
  Syntax(const Tokenizer& tkz, Environment* env, FilterBuilder<T>* bld)
      : tkz_(tkz), env_(env), bld_(bld), failed_(false), parens_(0) {}

  // Attempt a parse by recursive descent. The parse state is tracked in `env`.
  // Return null if the specification is invalid. On return, the `env` error data is updated if
  // there was a syntax mistake.
  std::optional<T> parse() {
    OptT filter{};
    ParseOpState state{};  // Need a new one for every parenthesis level.

    for (auto prev = ENV.begin(); !(ENV.at_end() || failed_); prev = ENV.cur()) {
      if (try_consume(tkz_.L_PARENS)) {
        ++parens_;
        try_parse(&Syntax::parse, &filter, &state);
        --parens_;
      }
      if (!ENV.at_end() && TOKEN == tkz_.R_PARENS) {
        if (parens_ > 0 && filter != std::nullopt && state.op == ParseOp::NONE &&
            state.negations == 0) {
          // End of current level of parenthesis. Return to the level above.
          ++ENV;
          return filter;
        }
        // Unmatched right parenthesis.
        return set_failed(ERROR_UNEXPECTED_R_PARENS);
      }
      if (!ENV.at_end() && try_consume(tkz_.NOT)) {
        ++state.negations;
      }
      if (!ENV.at_end() && TOKEN->one_of(tkz_.OR, tkz_.AND)) {
        if (filter == std::nullopt || state.op != ParseOp::NONE || state.negations > 0) {
          return set_failed(ERROR_UNEXPECTED_CONNECTIVE);
        }
        state.op = (TOKEN == tkz_.OR ? ParseOp::DISJ : ParseOp::CONJ);
        ++ENV;
      }

      // Try each type of expression in turn.
      try_parse(&Syntax::frame_length_expr, &filter, &state);
      // TODO(xianglong): Complete the implementation.

      if (failed_ && ENV.error_loc == std::nullopt) {
        // If error location is not set on failure, the error happened at `prev`.
        ENV.error_loc = prev;
      }
      if ((!failed_) && prev == ENV.cur()) {
        // Did not make progress, and yet did not fail. This is an unknown token.
        return set_failed(ERROR_UNKNOWN_KEYWORD);
      }
    }
    if (failed_) {
      return std::nullopt;
    }
    // A few extra syntax error conditions at the end of the current parenthesis.
    if (parens_ > 0) {
      ENV.error_cause = ERROR_UNMATCHED_L_PARENS;
      // Not setting error location since we want to point to the open parenthesis,
      // instead of here.
      failed_ = true;
      return std::nullopt;
    }
    if (state.op != ParseOp::NONE || state.negations > 0) {
      --ENV;
      return set_failed(ERROR_UNEXPECTED_CONNECTIVE);
    }
    return filter;
  }

 private:
  // `T` is type of filter constructed.
  using OptT = std::optional<T>;

  // Return the current token and advance the token cursor.
  inline TokenPtr consume() {
    TokenPtr result = TOKEN;
    ++ENV;
    return result;
  }

  // If the current token is one of those given as input, return true and advance the token cursor.
  template <typename... Ts>
  inline bool try_consume(TokenPtr tok, Ts... ts) {
    if (TOKEN->one_of(tok, ts...)) {
      ++ENV;
      return true;
    }
    return false;
  }

  // Attempt to parse a filter using the `fn` function. Combine its result with the `current`
  // filter.
  inline void try_parse(OptT (Syntax::*fn)(), OptT* current, ParseOpState* state) {
    ZX_ASSERT_MSG(state != nullptr, "Got nullptr for ParseOp state.");
    if (failed_) {
      return;
    }
    OptT parsed = (this->*fn)();
    if (parsed == std::nullopt) {
      return;
    }
    *current = create_filter(std::move(*current), std::move(parsed), state);
  }

  // Returns the negation of `filter` if the negation count is odd, or `filter` if it is even.
  // The negation count is reset to 0.
  inline OptT negate_filter(OptT filter, ParseOpState* state) {
    if ((state->negations) & 1) {
      state->negations = 0;
      return (filter == std::nullopt ? std::nullopt
                                     : std::optional(bld_->negation(std::move(*filter))));
    }
    state->negations = 0;
    return filter;
  }

  // Returns the logical composition of `left` and `right` filters with the `ParseOpState` op.
  // `op` is reset to `NONE` if `left` and `right` were composed.
  OptT compose_filters(OptT left, OptT right, ParseOpState* state) {
    if (right == std::nullopt) {
      return OptT{};
    }
    switch (state->op) {
      case ParseOp::NONE:
        if (left == std::nullopt) {
          // Initial state where `left` is null.
          return std::move(right);
        }
        // `left != nullopt` happens on a syntax error: two filters were juxtaposed
        // with no logical connective.
        ENV.error_cause = ERROR_REQUIRED_CONNECTIVE;
        failed_ = true;
        // Not setting `error_loc` as mistake actually happened at an earlier point.
        return OptT{};
      case ParseOp::CONJ:
        state->op = ParseOp::NONE;
        return bld_->conjunction(std::move(*left), std::move(*right));
      case ParseOp::DISJ:
        state->op = ParseOp::NONE;
        return bld_->disjunction(std::move(*left), std::move(*right));
      default:
        ZX_DEBUG_ASSERT_MSG(false, "Unexpected ParseOp state.");
        return OptT{};
    }
  }

  // Once the parsed filter has been constructed, compose it with the current filter with
  // operations given in `ParseOpState`.
  inline OptT create_filter(OptT current, OptT parsed, ParseOpState* state) {
    parsed = negate_filter(std::move(parsed), state);
    return compose_filters(std::move(current), std::move(parsed), state);
  }

  // Set the error cause and location in the parse environment, if no failure already.
  // Returns null so callers can simply return on the result of calling this function.
  inline OptT set_failed(const char* cause, TokenIterator loc) {
    if (!failed_) {
      ENV.error_cause = cause;
      ENV.error_loc = loc;
      failed_ = true;
    }
    return std::nullopt;
  }

  // Helper overload that sets the error location to the current location in the environment.
  inline OptT set_failed(const char* cause) { return set_failed(cause, ENV.cur()); }

  // Helper for constructing length filters.
  std::optional<uint16_t> length_value() {
    if (ENV.at_end()) {
      set_failed(ERROR_EXPECTED_LENGTH);
      return std::nullopt;  // Must return null explicitly for the right type.
    }
    size_t num_end;
    std::string input = TOKEN->get_term();
    long int num = stol(input, &num_end, 10);  // Base-10 number.
    if (num < 0 || num_end < input.length()) {
      set_failed(ERROR_INVALID_LENGTH);
      return std::nullopt;
    }
    ++ENV;
    return static_cast<uint16_t>(num);
  }

  OptT frame_length_expr() {
    if (ENV.at_end() || !TOKEN->one_of(tkz_.LESS, tkz_.GREATER)) {
      return std::nullopt;
    }
    TokenPtr comparator = consume();
    std::optional<uint16_t> length = length_value();
    if (length == std::nullopt) {
      return std::nullopt;
    }
    return bld_->frame_length(*length, comparator);
  }

  const Tokenizer& tkz_;
  Environment* env_;
  FilterBuilder<T>* bld_;
  // True once an error is encountered.
  bool failed_;
  // Count the depth of parentheses.
  size_t parens_;
#undef ENV
#undef TOKEN
};

}  // namespace netdump::parser

#endif  // ZIRCON_SYSTEM_UAPP_NETDUMP_PARSER_INTERNAL_H_
