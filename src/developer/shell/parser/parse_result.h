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
  ParseResult(const ParseResult&) = default;
  explicit ParseResult(std::string_view text) : ParseResult(text, 0, 0, 0, nullptr, nullptr) {}

  bool is_end() const { return frame_ == nullptr; }
  size_t offset() const { return offset_; }
  size_t errors() const { return errors_; }
  const std::shared_ptr<ParseResult>& error_alternative() const { return error_alternative_; }
  size_t parsed_successfully() const { return parsed_successfully_; }
  std::string_view unit() const { return unit_; }
  std::string_view tail() const { return unit_.substr(offset_); }
  std::shared_ptr<ast::Node> node() const { return frame_ ? frame_->node : nullptr; }

  // Move parsing ahead by len bytes, and push a token of that length.
  template <typename T = ast::Terminal>
  ParseResult Advance(size_t size) {
    if (is_end()) {
      return kEnd;
    }

    return ParseResult(unit_, offset_ + size, parsed_successfully_ + size, errors_,
                       std::make_shared<T>(offset_, size, tail().substr(0, size)), frame_);
  }

  // Set an error alternative on this parse result.
  ParseResult WithAlternative(ParseResult alternative) {
    ParseResult ret(*this);

    if (!ret.error_alternative_ ||
        ret.error_alternative_->parsed_successfully() < alternative.parsed_successfully()) {
      ret.error_alternative_ = std::make_shared<ParseResult>(std::move(alternative));
    }

    return ret;
  }

  // Rewrite the node in the current frame. Essentially this is a Pop(), then the result is passed
  // to the given closure, then the return value of the closure is Push()ed.
  //
  // See the parser-modifying version of Token() for example usage.
  ParseResult MapNode(fit::function<std::shared_ptr<ast::Node>(std::shared_ptr<ast::Node>)> f) {
    if (is_end()) {
      return kEnd;
    }

    auto ret = ParseResult(unit_, offset_, parsed_successfully_, errors_, f(node()), frame_->prev);

    if (error_alternative_) {
      ret.error_alternative_ =
          std::make_shared<ParseResult>(error_alternative_->MapNode(std::move(f)));
    }

    return ret;
  }

  // Insert an error indicating some form was expected. The parse position does not change.
  ParseResult Expected(std::string_view message) {
    if (is_end()) {
      return kEnd;
    }

    return ParseResult(unit_, offset_, parsed_successfully_, errors_ + 1,
                       std::make_unique<ast::Error>(offset_, 0, message), frame_);
  }

  // Skip the given number of bytes and push an error token indicating they were skipped.
  ParseResult Skip(size_t size, std::string_view message) {
    if (is_end()) {
      return kEnd;
    }

    return ParseResult(unit_, offset_ + size, parsed_successfully_, errors_ + 1,
                       std::make_unique<ast::Error>(offset_, size, message), frame_);
  }

  // Push a marker frame onto the stack. The next Reduce() call will reduce up to here.
  ParseResult Mark() {
    if (is_end()) {
      return kEnd;
    }

    auto ret = ParseResult(unit_, offset_, parsed_successfully_, errors_, nullptr, frame_);

    if (error_alternative_) {
      if (auto result = error_alternative_->Mark()) {
        ret.error_alternative_ = std::make_shared<ParseResult>(result);
      }
    }

    return ret;
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
      ParseResult ret(unit_, offset_, parsed_successfully_, errors_);
      ret.frame_ = frame;

      if (error_alternative_) {
        if (auto result = error_alternative_->DropMarker()) {
          ret.error_alternative_ = std::make_shared<ParseResult>(result);
        }
      }
      return ret;
    }

    return kEnd;
  }

  explicit operator bool() const { return !is_end(); }

  // A null parse result indicating no further error alternatives.
  static const ParseResult kEnd;

 private:
  ParseResult(std::string_view unit, size_t offset, size_t parsed_successfully, size_t errors)
      : offset_(offset),
        parsed_successfully_(parsed_successfully),
        errors_(errors),
        unit_(unit),
        frame_(nullptr) {}

  ParseResult(std::string_view unit, size_t offset, size_t parsed_successfully, size_t errors,
              std::shared_ptr<ast::Node> node, std::shared_ptr<Frame> prev)
      : ParseResult(unit, offset, parsed_successfully, errors) {
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

  // How many characters we've advanced past, not including characters skipped due to error.
  size_t parsed_successfully_;

  // Number of errors we've encountered.
  size_t errors_;

  // Text being parsed.
  std::string_view unit_;

  // Last node that was parsed at this position.
  std::shared_ptr<Frame> frame_;

  // An alternative to this parse result for error processing.
  std::shared_ptr<ParseResult> error_alternative_;
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

  auto ret = ParseResult(unit_, offset_, parsed_successfully_, errors_, node, cur);

  if (error_alternative_) {
    if (auto result = error_alternative_->Reduce<T>(pop_marker)) {
      ret.error_alternative_ = std::make_shared<ParseResult>(result);
    }
  }

  return ret;
}

}  // namespace shell::parser

#endif  // SRC_DEVELOPER_SHELL_PARSER_PARSE_RESULT_H_
