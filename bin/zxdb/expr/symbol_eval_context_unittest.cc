// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/symbol_eval_context.h"
#include "garnet/bin/zxdb/client/symbols/code_block.h"
#include "garnet/bin/zxdb/client/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/variable_test_support.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_node.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"

namespace zxdb {

namespace {

class SymbolEvalContextTest : public testing::Test {
 public:
  SymbolEvalContextTest() : provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) { loop_.Init(); }
  ~SymbolEvalContextTest() { loop_.Cleanup(); }

  DwarfExprEval& eval() { return eval_; }
  fxl::RefPtr<MockSymbolDataProvider>& provider() { return provider_; }
  debug_ipc::MessageLoop& loop() { return loop_; }

  fxl::RefPtr<CodeBlock> MakeCodeBlock() {
    auto block = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);

    // Declare a variable in this code block.
    auto variable = MakeUint64VariableForTest("present",
    0x1000, 0x2000, {llvm::dwarf::DW_OP_reg0});
    block->set_variables({LazySymbol(std::move(variable))});

    // TODO(brettw) this needs a type. Currently this test is very simple and
    // only outputs internal ints.
    return block;
  }

 private:
  DwarfExprEval eval_;
  debug_ipc::PlatformMessageLoop loop_;
  fxl::RefPtr<MockSymbolDataProvider> provider_;
};

}  // namespace

TEST_F(SymbolEvalContextTest, NotFoundSynchronous) {
  provider()->set_ip(0x1010);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      SymbolContext::ForRelativeAddresses(),
      provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  bool called = false;
  Err out_err;
  ExprValue out_value;
  eval_context->GetVariable(
      "not_present",
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
      });
  EXPECT_TRUE(called);
  EXPECT_TRUE(out_err.has_error());
  EXPECT_EQ(ExprValue(), out_value);
}

TEST_F(SymbolEvalContextTest, FoundSynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->set_ip(0x1010);
  provider()->AddRegisterValue(0, true, kValue);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      SymbolContext::ForRelativeAddresses(),
      provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  bool called = false;
  Err out_err;
  ExprValue out_value;
  eval_context->GetVariable("present", [&called, &out_err, &out_value](
                                           const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
  });
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(ExprValue(kValue), out_value);
}

TEST_F(SymbolEvalContextTest, FoundAsynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(0, false, kValue);
  provider()->set_ip(0x1010);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      SymbolContext::ForRelativeAddresses(),
      provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  bool called = false;
  Err out_err;
  ExprValue out_value;
  eval_context->GetVariable("present", [&called, &out_err, &out_value](
                                           const Err& err, ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  // Should not have been called yet since retrieving the register is
  // asynchronous.
  EXPECT_FALSE(called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(ExprValue(kValue), out_value);
}

// This is a larger test that runs the EvalContext through ExprNode.Eval.
TEST_F(SymbolEvalContextTest, NodeIntegation) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(0, false, kValue);
  provider()->set_ip(0x1010);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      SymbolContext::ForRelativeAddresses(),
      provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  // Look up an identifier that's not present.
  IdentifierExprNode present(ExprToken(ExprToken::Type::kName, "present", 0));
  bool called = false;
  Err out_err;
  ExprValue out_value;
  present.Eval(eval_context, [&called, &out_err, &out_value](const Err& err,
                                                             ExprValue value) {
    called = true;
    out_err = err;
    out_value = value;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  // Should not have been called yet since retrieving the register is
  // asynchronous.
  EXPECT_FALSE(called);

  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error());
  EXPECT_EQ(ExprValue(kValue), out_value);
}

}  // namespace zxdb
