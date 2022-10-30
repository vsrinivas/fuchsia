// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_MOCK_EXPR_NODE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_MOCK_EXPR_NODE_H_

#include "src/developer/debug/zxdb/expr/expr_node.h"

namespace zxdb {

// Custom ExprNode that just returns a known value, either synchronously or
// asynchronously.
class MockExprNode : public ExprNode {
 public:
  // Construct with fxl::MakeRefCounted().

  void EmitBytecode(VmStream& stream) const override;
  void Print(std::ostream& out, int indent) const override;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(MockExprNode);
  FRIEND_MAKE_REF_COUNTED(MockExprNode);

  MockExprNode(bool is_synchronous, ErrOrValue value);
  ~MockExprNode() override;

  bool is_synchronous_;
  ErrOrValue value_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_MOCK_EXPR_NODE_H_
