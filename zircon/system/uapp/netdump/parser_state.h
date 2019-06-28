// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This header defines internal structures used by the parser to help it manage state during
// a parse.

#ifndef ZIRCON_SYSTEM_UAPP_NETDUMP_PARSER_STATE_H_
#define ZIRCON_SYSTEM_UAPP_NETDUMP_PARSER_STATE_H_

#include <zircon/assert.h>

#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "tokens.h"

namespace netdump::parser {

using TokenIterator = std::vector<TokenPtr>::iterator;

// Parse state values for when a binary logical operator is encountered.
enum class ParseOp {
  NONE,
  CONJ,  // Logical `AND`.
  DISJ,  // Logical `OR`.
};

class ParseOpState {
 public:
  // Track if the parse has encountered a binary operation.
  ParseOp op = ParseOp::NONE;
  // Track how many negations the current parse is under.
  // The number of negations is tracked so e.g. `not not` can be differentiated from no negation.
  size_t negations = 0;
};

// An `Environment` object represents the parse environment. It can be seen as a state machine that
// keeps track of the parse cursor, i.e. the token location that the parser has reached.
// The states are positions along the series of tokens that need to be parser. If an error has been
// encountered within the parse, data relevant for reporting the error can be recorded here.
// Exactly one instance of `Environment` should be created at the beginning of a parse attempt for
// use until the end of the attempt. `Environment` is therefore movable but not copyable to help
// enforce this.
class Environment {
 public:
  // An instance is constructed from a vector of tokens that needs to be parsed.
  explicit Environment(std::vector<TokenPtr> tokens)
      : tokens_(std::move(tokens)), begin_(tokens_.begin()), cur_(begin_), end_(tokens_.end()) {}

  // Movable but not copyable.
  Environment(Environment&& other) = default;
  Environment& operator=(Environment&& other) = default;
  Environment(const Environment&) = delete;
  Environment& operator=(const Environment&) = delete;

  // Return the first token location to be parsed.
  [[nodiscard]] inline TokenIterator begin() const { return begin_; }

  // Return the current token location under parse.
  [[nodiscard]] inline TokenIterator cur() const { return cur_; }

  // Return the location beyond the last token to be parsed.
  [[nodiscard]] inline TokenIterator end() const { return end_; }

  [[nodiscard]] inline bool at_end() const { return cur_ == end_; }

  // Reset the token location under parse to the beginning.
  // Does not clear the error data.
  inline void reset() { cur_ = begin_; }

  // Clear the error data.
  inline void clear_error() {
    error_cause.clear();
    error_loc = std::nullopt;
  }

  inline bool has_error() { return (error_loc != std::nullopt); }

  // Operators to get the token at the current location, and to change the location.
  TokenPtr operator*() {
    ZX_DEBUG_ASSERT_MSG(cur_ < end_, "Dereferencing end token location.");
    return *cur_;
  }
  Environment& operator++() {
    if (cur_ < end_) {
      ++cur_;
    }
    return (*this);
  }
  Environment& operator--() {
    if (cur_ > begin_) {
      --cur_;
    }
    return (*this);
  }

  // Data for any error that was encountered.
  std::string error_cause{};
  std::optional<TokenIterator> error_loc{};

 private:
  std::vector<TokenPtr> tokens_;
  TokenIterator begin_;
  TokenIterator cur_;
  TokenIterator end_;
};

}  // namespace netdump::parser

#endif  // ZIRCON_SYSTEM_UAPP_NETDUMP_PARSER_STATE_H_
