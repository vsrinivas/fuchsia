// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_PARSER_PARSE_RESULT_H_
#define SRC_DEVELOPER_SHELL_PARSER_PARSE_RESULT_H_

#include <set>
#include <string_view>

#include "lib/fit/function.h"
#include "src/developer/shell/parser/ast.h"

namespace shell::parser {

// The result of parsing.
class ParseResult {
  // Frame in a stack of parsed nodes.
  struct Frame {
    std::shared_ptr<ast::Node> node;
    std::shared_ptr<Frame> prev;

    bool is_marker_frame() const { return !node; }
    bool is_stack_bottom() const { return !prev; }
  };

 public:
  ParseResult(const ParseResult &) = default;
  explicit ParseResult(std::string_view text) : ParseResult(text, 0, 0, 0, 0, nullptr, nullptr) {}

  bool is_end() const { return frame_ == nullptr; }
  size_t offset() const { return offset_; }
  size_t error_score() const { return error_internal_ + std::max(error_insert_, error_delete_); }
  std::string_view unit() const { return unit_; }
  std::string_view tail() const { return unit_.substr(offset_); }
  std::shared_ptr<ast::Node> node() const { return frame_ ? frame_->node : nullptr; }

  // Move parsing ahead by len bytes, and push a token of that length.
  template <typename T = ast::Terminal>
  ParseResult Advance(size_t size) {
    if (is_end()) {
      return kEnd;
    }

    return ParseResult(unit_, offset_ + size, 0, 0, error_score(),
                       std::make_shared<T>(offset_, size, tail().substr(0, size)), frame_);
  }

  // Swap the node in the current frame. Essentially this is a Pop() followed by a Push(). Usually
  // the new node will be some modification of the current node, but instead of popping, modifying,
  // and pushing, it's usually more efficient to peek the current node with node(), create the
  // modified node, then do all the stack alteration in one operation.
  //
  // An example use case would be the parser-modifying version of Token(), which parses a new
  // non-terminal on the stack, then concatenates its children to create a new terminal instead.
  ParseResult SetNode(std::shared_ptr<ast::Node> node) {
    if (is_end()) {
      return kEnd;
    }

    return ParseResult(unit_, offset_, error_insert_, error_delete_, error_internal_, node,
                       frame_->prev);
  }

  // Insert an error indicating a token of the given size was expected. ident names the token in the
  // error message. The parse position does not change.
  ParseResult Expected(size_t size, std::string_view message) {
    if (is_end()) {
      return kEnd;
    }

    return ParseResult(unit_, offset_, error_insert_ + size, error_delete_, error_internal_,
                       std::make_unique<ast::Error>(offset_, 0, message), frame_);
  }

  // Skip the given number of bytes and push an error token indicating they were skipped.
  ParseResult Skip(size_t size, std::string_view message) {
    if (is_end()) {
      return kEnd;
    }

    return ParseResult(unit_, offset_ + size, error_insert_, error_delete_ + size, error_internal_,
                       std::make_unique<ast::Error>(offset_, size, message), frame_);
  }

  // Push a marker frame onto the stack. The next Reduce() call will reduce up to here.
  ParseResult Mark() {
    if (is_end()) {
      return kEnd;
    }

    return ParseResult(unit_, offset_, error_insert_, error_delete_, error_internal_, nullptr,
                       frame_);
  }

  // Pops from the stack until a marker frame or the top of the stack is encountered, then produces
  // a single nonterminal from the results and pushes that.
  //
  // This is how all nonterminals are created:
  // 1) A marker frame is pushed onto the stack.
  // 2) Assorted parsers are run, pushing the children of the node onto the stack as they go.
  // 3) Reduce() is called and turns the nodes between the marker and the stack top into a new
  //    nonterminal.
  //
  // If pop_marker is false, we will not remove the marker frame when we pop. This is useful for
  // building multiple non-terminals from the same reduction point, as the LAssoc combinator will.
  template <typename T>
  ParseResult Reduce(bool pop_marker = true);

  // Remove the marker frame nearest the top of the stack without disturbing the rest of the stack.
  // This is useful if we call reduce with pop_marker = false.
  ParseResult DropMarker() {
    if (auto frame = DropMarkerFrame()) {
      ParseResult ret(unit_, offset_, error_insert_, error_delete_, error_internal_);
      ret.frame_ = frame;
      return ret;
    }

    return kEnd;
  }

  explicit operator bool() const { return !is_end(); }

  // A null parse result indicating no further error alternatives.
  static const ParseResult kEnd;

 private:
  ParseResult(std::string_view unit, size_t offset, size_t error_insert, size_t error_delete,
              size_t error_internal)
      : offset_(offset),
        error_insert_(error_insert),
        error_delete_(error_delete),
        error_internal_(error_internal),
        unit_(unit),
        frame_(nullptr) {}

  ParseResult(std::string_view unit, size_t offset, size_t error_insert, size_t error_delete,
              size_t error_internal, std::shared_ptr<ast::Node> node, std::shared_ptr<Frame> prev)
      : ParseResult(unit, offset, error_insert, error_delete, error_internal) {
    frame_ = std::make_shared<Frame>(Frame{.node = std::move(node), .prev = std::move(prev)});
  }

  // Implement the core of DropMarker by actually walking the stack and deleting the marker frame.
  std::shared_ptr<Frame> DropMarkerFrame(std::shared_ptr<Frame> frame = nullptr) {
    if (!frame) {
      frame = frame_;
    }

    if (!frame) {
      return nullptr;
    } else if (frame->is_stack_bottom()) {
      return frame;
    } else if (frame->is_marker_frame()) {
      return frame->prev;
    } else {
      return std::make_shared<Frame>(
          Frame{.node = frame->node, .prev = DropMarkerFrame(frame->prev)});
    }
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

  // Text being parsed.
  std::string_view unit_;

  // Last node that was parsed at this position.
  std::shared_ptr<Frame> frame_;
};

template <typename T>
ParseResult ParseResult::Reduce(bool pop_marker) {
  std::vector<std::shared_ptr<ast::Node>> children;
  std::shared_ptr<Frame> cur;

  if (!frame_) {
    return kEnd;
  }

  // We have a dummy marker frame at the bottom of the stack, so we'll always hit the end conditon
  // here.
  for (cur = frame_; !cur->is_marker_frame(); cur = cur->prev) {
    if (!cur->node->IsWhitespace()) {
      children.push_back(cur->node);
    }
  }

  // If we didn't arrive at the beginning of the stack, also pop the marker (unless we were told not
  // to by the caller).
  if (pop_marker && !cur->is_stack_bottom()) {
    cur = cur->prev;
  }

  std::reverse(children.begin(), children.end());

  auto start = offset_;

  if (!children.empty()) {
    start = children.front()->start();
  }

  auto node = std::make_shared<T>(start, std::move(children));

  return ParseResult(unit_, offset_, error_insert_, error_delete_, error_internal_, node, cur);
}

}  // namespace shell::parser

#endif  // SRC_DEVELOPER_SHELL_PARSER_PARSE_RESULT_H_
