// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_operators.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/mock_expr_node.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class EvalOperators : public TestWithLoop {
 public:
  EvalOperators() : eval_context_(fxl::MakeRefCounted<MockEvalContext>()) {}
  ~EvalOperators() = default;

  fxl::RefPtr<MockEvalContext>& eval_context() { return eval_context_; }

 private:
  fxl::RefPtr<MockEvalContext> eval_context_;
};

void QuitNow() { debug_ipc::MessageLoop::Current()->QuitNow(); }

}  // namespace

TEST_F(EvalOperators, Assignment) {
  auto int32_type = MakeInt32Type();

  // The casting test provides most tests for conversions so this test just
  // checks that the correct values are written and returned.
  constexpr uint64_t kAddress = 0x98723461923;
  ExprValue dest(int32_type, {0, 0, 0, 0}, ExprValueSource(kAddress));
  auto dest_node = fxl::MakeRefCounted<MockExprNode>(false, dest);

  ExprToken assign(ExprTokenType::kEquals, "=", 0);

  std::vector<uint8_t> data{0x12, 0x34, 0x56, 0x78};
  ExprValue source(int32_type, data, ExprValueSource());
  auto source_node = fxl::MakeRefCounted<MockExprNode>(false, source);

  bool called = false;
  Err out_err;
  ExprValue out_value;
  EvalBinaryOperator(
      eval_context(), dest_node, assign, source_node,
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
        QuitNow();
      });

  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);

  // Written value returned.
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(source, out_value);

  // Memory written to target.
  auto mem_writes = eval_context()->data_provider()->GetMemoryWrites();
  ASSERT_EQ(1u, mem_writes.size());
  EXPECT_EQ(kAddress, mem_writes[0].first);
  EXPECT_EQ(data, mem_writes[0].second);
}

}  // namespace zxdb
