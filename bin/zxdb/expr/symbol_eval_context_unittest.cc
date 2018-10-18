// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/symbol_eval_context.h"
#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/expr/expr_node.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/code_block.h"
#include "garnet/bin/zxdb/symbols/collection.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/inherited_from.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "garnet/bin/zxdb/symbols/variable_test_support.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"

namespace zxdb {

namespace {

class SymbolEvalContextTest : public testing::Test {
 public:
  SymbolEvalContextTest()
      : provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) {
    loop_.Init();
  }
  ~SymbolEvalContextTest() { loop_.Cleanup(); }

  DwarfExprEval& eval() { return eval_; }
  fxl::RefPtr<MockSymbolDataProvider>& provider() { return provider_; }
  debug_ipc::MessageLoop& loop() { return loop_; }

  fxl::RefPtr<CodeBlock> MakeCodeBlock() {
    auto block = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);

    // Declare a variable in this code block stored in register 0.
    auto variable = MakeUint64VariableForTest(
        "present", 0x1000, 0x2000,
        {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});
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
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  bool called = false;
  Err out_err;
  ExprValue out_value;
  eval_context->GetNamedValue(
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
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  bool called = false;
  Err out_err;
  ExprValue out_value;
  eval_context->GetNamedValue("present", [&called, &out_err, &out_value](
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
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  bool called = false;
  Err out_err;
  ExprValue out_value;
  eval_context->GetNamedValue("present", [&called, &out_err, &out_value](
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
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(ExprValue(kValue), out_value);
}

// Tests finding variables on |this| and subclasses of |this|.
TEST_F(SymbolEvalContextTest, FoundThis) {
  auto int32_type = MakeInt32Type();
  auto derived = MakeDerivedClassPair(
      Symbol::kTagClassType, "Base", {{"b1", int32_type}, {"b2", int32_type}},
      "Derived", {{"d1", int32_type}, {"d2", int32_type}});

  auto derived_ptr = fxl::MakeRefCounted<ModifiedType>(Symbol::kTagPointerType,
                                                       LazySymbol(derived));

  // Make the storage for the class in memory.
  constexpr uint64_t kObjectAddr = 0x3000;
  constexpr uint8_t kB1 = 1;
  constexpr uint8_t kB2 = 2;
  constexpr uint8_t kD1 = 3;
  constexpr uint8_t kD2 = 4;
  provider()->AddMemory(kObjectAddr, {kB1, 0, 0, 0,    // (int32) Base.b1
                                      kB2, 0, 0, 0,    // (int32) Base.b2
                                      kD1, 0, 0, 0,    // (int32) Derived.d1
                                      kD2, 0, 0, 0});  // (int32) Derived.d2

  // Our parameter "Derived* this = kObjectAddr" is passed in register 0.
  provider()->set_ip(0x1000);
  provider()->AddRegisterValue(0, false, kObjectAddr);
  auto this_var = MakeVariableForTest(
      "this", derived_ptr, 0x1000, 0x2000,
      {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});

  // Make a function with a parameter / object pointer to Derived (this will be
  // like a member function on Derived).
  auto function = fxl::MakeRefCounted<Function>();
  function->set_parameters({LazySymbol(this_var)});
  function->set_object_pointer(LazySymbol(this_var));

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      SymbolContext::ForRelativeAddresses(), provider(), function);
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  // First get d2 on the derived class. "this" should be implicit.
  bool called = false;
  Err out_err;
  ExprValue out_value;
  eval_context->GetNamedValue(
      "d2", [&called, &out_err, &out_value](const Err& err, ExprValue value) {
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
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(kD2)), out_value);

  // Now get b2 on the base class, it should implicitly find it on "this"
  // and then check the base class.
  called = false;
  out_err = Err();
  out_value = ExprValue();
  eval_context->GetNamedValue(
      "b2", [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
        debug_ipc::MessageLoop::Current()->QuitNow();
      });
  EXPECT_FALSE(called);
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(kB2)), out_value);
}

// This is a larger test that runs the EvalContext through ExprNode.Eval.
TEST_F(SymbolEvalContextTest, NodeIntegation) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(0, false, kValue);
  provider()->set_ip(0x1010);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  // Look up an identifier that's not present.
  auto present = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprToken::Type::kName, "present", 0));
  bool called = false;
  Err out_err;
  ExprValue out_value;
  present->Eval(eval_context, [&called, &out_err, &out_value](const Err& err,
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
