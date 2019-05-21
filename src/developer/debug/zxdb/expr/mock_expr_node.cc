// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/mock_expr_node.h"

#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

MockExprNode::MockExprNode(bool is_synchronous, ExprValue value)
    : is_synchronous_(is_synchronous), value_(value) {}

MockExprNode::~MockExprNode() = default;

void MockExprNode::Eval(fxl::RefPtr<ExprEvalContext> context,
                        EvalCallback cb) const {
  if (is_synchronous_) {
    cb(Err(), value_);
  } else {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [value = value_, cb]() { cb(Err(), value); });
  }
}

void MockExprNode::Print(std::ostream& out, int indent) const {}

}  // namespace zxdb
