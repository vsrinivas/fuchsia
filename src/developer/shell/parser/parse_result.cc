// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parse_result.h"

namespace shell::parser {

ParseResult ParseResult::Reduce(const std::optional<std::string_view>& name) {
  std::vector<std::shared_ptr<ast::Node>> children;
  std::shared_ptr<Frame> cur;

  if (!frame_) {
    return End();
  }

  // We have a dummy marker frame at the bottom of the stack, so we'll always hit the end conditon
  // here.
  for (cur = frame_; !cur->is_marker_frame(); cur = cur->prev) {
    if (!cur->node->is_whitespace()) {
      children.push_back(cur->node);
    }
  }

  // If we didn't arrive at the beginning of the stack, also pop the marker.
  if (!cur->is_stack_bottom()) {
    cur = cur->prev;
  }

  std::reverse(children.begin(), children.end());

  auto start = offset_;

  if (!children.empty()) {
    start = children.front()->start();
  }

  auto node = std::make_shared<ast::Nonterminal>(start, std::move(children));
  if (name) {
    node->set_name(*name);
  }

  return ParseResult(tail_, offset_, error_insert_, error_delete_, error_internal_, node, cur);
}

ParseResultStream ParseResultStream::Follow(fit::function<ParseResultStream(ParseResult)> next) && {
  // Takes the least-erroneous result from this stream and uses it as the prefix for the given
  // parser. In the future lots of interesting error handling stuff will happen here (backtracking!)
  // but for now this will do.
  return next(Next());
}

}  // namespace shell::parser
