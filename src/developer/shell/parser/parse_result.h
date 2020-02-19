// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_PARSER_PARSE_RESULT_H_
#define SRC_DEVELOPER_SHELL_PARSER_PARSE_RESULT_H_

#include <string_view>

#include "lib/fit/function.h"
#include "src/developer/shell/parser/ast.h"

namespace shell::parser {

// A particular parse which is emitted from a ParseResultStream. Successful parses will only
// produce one of these, but error parses may produce multiple recovery parses.
class ParseResult {
  // Frame in a stack of parsed nodes.
  struct Frame {
    std::shared_ptr<ast::Node> node;
    std::shared_ptr<Frame> prev;

    bool is_marker_frame() const { return !node; }
    bool is_stack_bottom() const { return !prev; }
  };

 public:
  explicit ParseResult(std::string_view text) : ParseResult(text, 0, 0, 0, 0, nullptr, nullptr) {}

  size_t offset() const { return offset_; }
  size_t error_score() const { return error_internal_ + std::max(error_insert_, error_delete_); }
  std::string_view tail() const { return tail_; }
  std::shared_ptr<ast::Node> node() const { return frame_ ? frame_->node : nullptr; }

  // Move parsing ahead by len bytes, and push a token of that length.
  ParseResult Advance(size_t size) {
    return ParseResult(tail_.substr(size), offset_ + size, 0, 0, error_score(),
                       std::make_shared<ast::Terminal>(offset_, size), frame_);
  }

  // Insert an error indicating a token of the given size was expected. ident names the token in the
  // error message. The parse position does not change.
  ParseResult Expected(size_t size, const std::string& ident) {
    return ParseResult(tail_, offset_, error_insert_ + size, error_delete_, error_internal_,
                       std::make_unique<ast::Error>(offset_, 0, "Expected " + ident), frame_);
  }

  // Skip the given number of bytes and push an error token indicating they were skipped.
  ParseResult Skip(size_t size) {
    return ParseResult(
        tail_.substr(size), offset_ + size, error_insert_, error_delete_ + size, error_internal_,
        std::make_unique<ast::Error>(offset_, size,
                                     "Unexpected '" + std::string(tail().substr(0, size)) + "'"),
        frame_);
  }

  // A null parse result for this position indicating no further error alternatives.
  ParseResult End() {
    return ParseResult(tail_, offset_, error_insert_, error_delete_, error_internal_);
  }

  // Pops from the stack until a marker frame or the top of the stack is encountered, then produces
  // a single nonterminal from the results and pushes that.
  //
  // This is how all nonterminals are created:
  // 1) A marker frame is pushed onto the stack.
  // 2) Assorted parsers are run, pushing the children of the node onto the stack as they go.
  // 3) Reduce() is called and turns the nodes between the marker and the stack top into a new
  //    nonterminal.
  ParseResult Reduce(const std::optional<std::string_view>& name = std::nullopt);

  operator bool() const { return frame_ != nullptr; }

 private:
  friend class ParseResultStream;

  ParseResult(std::string_view tail, size_t offset, size_t error_insert, size_t error_delete,
              size_t error_internal)
      : offset_(offset),
        error_insert_(error_insert),
        error_delete_(error_delete),
        error_internal_(error_internal),
        tail_(tail),
        frame_(nullptr) {}

  ParseResult(std::string_view tail, size_t offset, size_t error_insert, size_t error_delete,
              size_t error_internal, std::shared_ptr<ast::Node> node, std::shared_ptr<Frame> prev)
      : ParseResult(tail, offset, error_insert, error_delete, error_internal) {
    frame_ = std::make_shared<Frame>(Frame{.node = std::move(node), .prev = std::move(prev)});
  }

  // Position from the beginning of the parsed text.
  size_t offset_;

  // These are our three error-score values.
  //
  // If either error_insert_ or error_delete_ is nonzero, then the most recently parsed tokens
  // represented or contained errors. error_insert is the number of characters that were inserted to
  // correct errors, and error_delete is the number of characters that were removed to correct
  // errors. error_internal_ represents the score of errors which are no longer part of the most
  // recent run, i.e. errors that were parsed where normal parsing resumed afterward.
  //
  // If, from this state, a token parses successfully, error_internal increases by the *maximum* of
  // error_insert_ and error_delete_, which both become zero. This makes error_internal_ consistent
  // with the Levenshtein string distance.
  size_t error_insert_;
  size_t error_delete_;
  size_t error_internal_;

  // Text remaining to parse.
  std::string_view tail_;

  // Last node that was parsed at this position.
  std::shared_ptr<Frame> frame_;
};

// Result of a parse operation. Produces one or more ParseResult values in order of increasing
// amount of error consumed.
//
// ParseResultStream is a single-use iterator. You call Next() on it to retrieve the results it
// yields until it yields a null result, at which point it will never yield a valid result again.
// For this reason, code that queries a ParseResultStream should usually consume it. Several methods
// on ParseResultStream produce a new ParseResultStream that modifies the results, and those methods
// always require an RValue receiver to ensure semantics are correct.
class ParseResultStream {
 public:
  ParseResultStream(bool ok, fit::function<ParseResult()> next) : ok_(ok), next_(std::move(next)) {}
  ParseResultStream(const char* text) : ParseResultStream(std::string_view(text)) {}
  ParseResultStream(std::string_view text) : ParseResultStream(ParseResult(text)) {}
  explicit ParseResultStream(ParseResult result)
      : ok_(true), next_([result]() mutable {
          auto ret = result;
          result = result.End();
          return ret;
        }) {}

  // Whether the parse succeeded. If so, the first call to Next() will yield an error-free parse
  // (though if the start state already aggregated error, it may still have an error score).
  bool ok() { return ok_; }

  // Retrieve the next element from the stream.
  ParseResult Next() { return next_(); }

  // Attempt to parse the tail left after each parse result in this stream with the results of
  // another parser.
  ParseResultStream Follow(fit::function<ParseResultStream(ParseResult)> next) &&;

 private:
  bool ok_;
  fit::function<ParseResult()> next_;
};

}  // namespace shell::parser

#endif  // SRC_DEVELOPER_SHELL_PARSER_PARSE_RESULT_H_
