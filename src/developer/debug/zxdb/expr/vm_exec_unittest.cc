// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/vm_exec.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/expr/vm_stream.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

namespace {

class VmExecTest : public TestWithLoop {
 public:
  void CallVmExec(VmStream stream, bool expected_sync, ErrOrValue expected) {
    fxl::RefPtr<EvalContext> call_context(eval_context_);

    bool called = false;
    VmExec(call_context, std::move(stream), [&](ErrOrValue result) mutable {
      called = true;
      EXPECT_EQ(expected, result);
    });

    EXPECT_EQ(expected_sync, called);

    if (!called) {
      debug::MessageLoop::Current()->RunUntilNoTasks();
      EXPECT_TRUE(called) << "Callback lost";
    }
  }

  // Expects success = to the given integer value (flexible on type).
  void CallVmExec(VmStream stream, bool expected_sync, int64_t expected) {
    fxl::RefPtr<EvalContext> call_context(eval_context_);

    bool called = false;
    VmExec(call_context, std::move(stream), [&](ErrOrValue result) mutable {
      called = true;
      ASSERT_TRUE(result.ok()) << result.err().msg();

      int64_t result_value = 0;
      result.value().PromoteTo64(&result_value);
      EXPECT_EQ(expected, result_value);
    });

    EXPECT_EQ(expected_sync, called);

    if (!called) {
      debug::MessageLoop::Current()->RunUntilNoTasks();
      EXPECT_TRUE(called) << "Callback lost";
    }
  }

 protected:
  fxl::RefPtr<MockEvalContext> eval_context_ = fxl::MakeRefCounted<MockEvalContext>();
};

}  // namespace

TEST_F(VmExecTest, Empty) {
  // An empty program is defined to produce an empty value.
  CallVmExec(VmStream(), true, ExprValue());
}

TEST_F(VmExecTest, UnaryOp) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(27)));
  stream.push_back(VmOp::MakeUnary(ExprToken(ExprTokenType::kBang, "!", 0)));
  CallVmExec(std::move(stream), true, 0);
}

TEST_F(VmExecTest, BinaryOp) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));
  stream.push_back(VmOp::MakeLiteral(ExprValue(27)));
  stream.push_back(VmOp::MakeBinary(ExprToken(ExprTokenType::kPlus, "+", 0)));
  CallVmExec(std::move(stream), true, 32);
}

TEST_F(VmExecTest, ExpandRef) {
  // Inject the data to back the reference (32-bit little-endian value = 99).
  uint64_t kDataAddress = 0x32745612;
  eval_context_->data_provider()->AddMemory(kDataAddress, {99, 0, 0, 0});

  // Make the reference.
  auto int32_type = MakeInt32Type();
  auto int32_ref_type = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kReferenceType, int32_type);
  ExprValue ref_value(kDataAddress, int32_ref_type);

  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ref_value));
  stream.push_back(VmOp::MakeExpandRef());
  CallVmExec(std::move(stream), false, 99);
}

TEST_F(VmExecTest, Drop) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));
  stream.push_back(VmOp::MakeLiteral(ExprValue(27)));
  stream.push_back(VmOp::MakeDrop());
  CallVmExec(std::move(stream), true, 5);  // Should be left with the first value only.
}

TEST_F(VmExecTest, Dup) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));
  stream.push_back(VmOp::MakeDup());
  stream.push_back(VmOp::MakeBinary(ExprToken(ExprTokenType::kPlus, "+", 0)));
  CallVmExec(std::move(stream), true, 10);
}

TEST_F(VmExecTest, Jump) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));
  stream.push_back(VmOp::MakeJump(3));  // Skip to last instruction (one past the end).
  stream.push_back(VmOp::MakeLiteral(ExprValue(6)));  // Should be skipped.
  CallVmExec(std::move(stream), true, 5);
}

TEST_F(VmExecTest, JumpIfFalse) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));
  stream.push_back(VmOp::MakeJumpIfFalse(3));         // Skip to last instruction.
  stream.push_back(VmOp::MakeLiteral(ExprValue(6)));  // Should be run.
  CallVmExec(std::move(stream), true, 6);

  VmStream stream2;
  stream2.push_back(VmOp::MakeLiteral(ExprValue(1)));
  stream2.push_back(VmOp::MakeLiteral(ExprValue(0)));
  stream2.push_back(VmOp::MakeJumpIfFalse(4));         // Skip to pushing "7".
  stream2.push_back(VmOp::MakeLiteral(ExprValue(6)));  // Should not be run.
  stream2.push_back(VmOp::MakeLiteral(ExprValue(7)));  // Should be run.
  stream2.push_back(VmOp::MakeBinary(ExprToken(ExprTokenType::kPlus, "+", 0)));
  CallVmExec(std::move(stream2), true, 8);  // 7 + 1
}

TEST_F(VmExecTest, LocalVar) {
  // Makes two local variables and adds them.
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(1)));
  stream.push_back(VmOp::MakeSetLocal(2));
  stream.push_back(VmOp::MakeLiteral(ExprValue(9)));
  stream.push_back(VmOp::MakeSetLocal(4));
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));  // Overwrite slot 2.
  stream.push_back(VmOp::MakeSetLocal(2));
  stream.push_back(VmOp::MakeGetLocal(2));
  stream.push_back(VmOp::MakeGetLocal(4));
  stream.push_back(VmOp::MakeBinary(ExprToken(ExprTokenType::kPlus, "+", 0)));
  CallVmExec(std::move(stream), true, 14);  // 5 + 9 = 14.

  // Create local variables and shrink the stack.
  ExprToken var0(ExprTokenType::kName, "i", 0);
  ExprToken var1(ExprTokenType::kName, "j", 2);
  stream = VmStream();
  stream.push_back(VmOp::MakeLiteral(ExprValue(1)));
  stream.push_back(VmOp::MakeSetLocal(0, var0));
  stream.push_back(VmOp::MakeLiteral(ExprValue(2)));
  stream.push_back(VmOp::MakeSetLocal(1, var1));
  stream.push_back(VmOp::MakePopLocals(1));       // Shrinks the local variable stack down to 1.
  stream.push_back(VmOp::MakeGetLocal(0, var0));  // This should still succeed.
  stream.push_back(VmOp::MakeGetLocal(1, var1));  // This should now fail.
  CallVmExec(std::move(stream), true, Err("Bad local variable index 1 when reading 'j'."));

  // Create a local variable, set it, then dereference the variable for the result. This tests the
  // end-to-end update of these loacals.
  stream = VmStream();
  stream.push_back(VmOp::MakeLiteral(ExprValue(1)));
  stream.push_back(VmOp::MakeSetLocal(0, var0));
  stream.push_back(VmOp::MakeGetLocal(0, var0));       // Left side of "operator=".
  stream.push_back(VmOp::MakeLiteral(ExprValue(99)));  // Right side of "operator=".
  stream.push_back(VmOp::MakeBinary(ExprToken(ExprTokenType::kEquals, "=", 0)));
  stream.push_back(VmOp::MakeDrop());             // Drop unneeded return value of "operator=".
  stream.push_back(VmOp::MakeGetLocal(0, var0));  // Read local to see if it got updated.
  CallVmExec(std::move(stream), true, 99);        // Result should be the updated value.
}

TEST_F(VmExecTest, Callback1) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));
  stream.push_back(VmOp::MakeCallback1(
      [&](const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& param) -> ErrOrValue {
        EXPECT_EQ(param, ExprValue(5));
        return ExprValue(1234);
      }));
  CallVmExec(std::move(stream), true, 1234);
}

TEST_F(VmExecTest, Callback2) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));
  stream.push_back(VmOp::MakeLiteral(ExprValue(6)));
  stream.push_back(
      VmOp::MakeCallback2([&](const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& param1,
                              const ExprValue& param2) -> ErrOrValue {
        EXPECT_EQ(param1, ExprValue(5));
        EXPECT_EQ(param2, ExprValue(6));
        return ExprValue(1234);
      }));
  CallVmExec(std::move(stream), true, 1234);
}

TEST_F(VmExecTest, AsyncCallback1) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));
  stream.push_back(VmOp::MakeAsyncCallback1(
      [&](const fxl::RefPtr<EvalContext>& eval_context, const ExprValue& param, EvalCallback cb) {
        EXPECT_EQ(param, ExprValue(5));

        // Evaluate the completion asynchonosly.
        debug::MessageLoop::Current()->PostTask(
            FROM_HERE, [cb = std::move(cb)]() mutable { cb(ExprValue(1234)); });
      }));
  CallVmExec(std::move(stream), false, 1234);
}

TEST_F(VmExecTest, AsyncCallback2) {
  VmStream stream;
  stream.push_back(VmOp::MakeLiteral(ExprValue(5)));
  stream.push_back(VmOp::MakeLiteral(ExprValue(6)));
  stream.push_back(VmOp::MakeAsyncCallback2([&](const fxl::RefPtr<EvalContext>& eval_context,
                                                const ExprValue& param1, const ExprValue& param2,
                                                EvalCallback cb) {
    EXPECT_EQ(param1, ExprValue(5));
    EXPECT_EQ(param2, ExprValue(6));

    // Evaluate the completion asynchonosly.
    debug::MessageLoop::Current()->PostTask(
        FROM_HERE, [cb = std::move(cb)]() mutable { cb(ExprValue(1234)); });
  }));
  CallVmExec(std::move(stream), false, 1234);
}

}  // namespace zxdb
