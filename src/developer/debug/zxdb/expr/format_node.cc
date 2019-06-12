// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/format_node.h"

namespace zxdb {

FormatNode::FormatNode() : weak_factory_(this) {}

FormatNode::FormatNode(const std::string& name, ExprValue value)
    : source_(kValue),
      state_(kHasValue),
      name_(name),
      value_(std::move(value)),
      weak_factory_(this) {}

FormatNode::FormatNode(const std::string& expression)
    : source_(kExpression),
      state_(kUnevaluated),
      expression_(expression),
      weak_factory_(this) {}

FormatNode::~FormatNode() = default;

fxl::WeakPtr<FormatNode> FormatNode::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

void FormatNode::SetValue(ExprValue v) { value_ = std::move(v); }

}  // namespace zxdb
