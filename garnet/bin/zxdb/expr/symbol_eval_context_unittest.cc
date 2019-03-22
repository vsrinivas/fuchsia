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
#include "garnet/bin/zxdb/symbols/data_member.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/inherited_from.h"
#include "garnet/bin/zxdb/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/symbols/modified_type.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "garnet/bin/zxdb/symbols/variable_test_support.h"
#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/shared/platform_message_loop.h"

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
    auto block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);

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

struct ValueResult {
  bool called = false;  // Set when the callback is issued.
  Err err;
  ExprValue value;
  fxl::RefPtr<Symbol> symbol;
};

// Indicates whether GetNamedValue should exit the message loop when the
// callback is issued. Synchronous results don't need this.
enum GetNamedValueAsync { kQuitLoop, kSynchronous };

// Wrapper around eval_context->GetNamedValue that places the callback
// parameters into a struct. It makes the callsites cleaner.
void GetNamedValue(fxl::RefPtr<ExprEvalContext>& eval_context,
                   const std::string& name, GetNamedValueAsync async,
                   ValueResult* result) {
  auto [err, ident] = Identifier::FromString(name);
  ASSERT_FALSE(err.has_error());

  eval_context->GetNamedValue(
      ident, [result, async](const Err& err, fxl::RefPtr<Symbol> symbol,
                             ExprValue value) {
        result->called = true;
        result->err = err;
        result->value = std::move(value);
        result->symbol = std::move(symbol);
        if (async == kQuitLoop)
          debug_ipc::MessageLoop::Current()->QuitNow();
      });
}

const debug_ipc::RegisterID kDWARFReg0ID = debug_ipc::RegisterID::kARMv8_x0;
const debug_ipc::RegisterID kDWARFReg1ID = debug_ipc::RegisterID::kARMv8_x1;

}  // namespace

TEST_F(SymbolEvalContextTest, NotFoundSynchronous) {
  provider()->set_ip(0x1010);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      fxl::WeakPtr<const ProcessSymbols>(),
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  ValueResult result;
  GetNamedValue(eval_context, "not_present", kSynchronous, &result);

  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.err.has_error());
  EXPECT_EQ(ExprValue(), result.value);
  EXPECT_FALSE(result.symbol);
}

TEST_F(SymbolEvalContextTest, FoundSynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->set_ip(0x1010);
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      fxl::WeakPtr<const ProcessSymbols>(),
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  ValueResult result;
  GetNamedValue(eval_context, "present", kSynchronous, &result);

  EXPECT_TRUE(result.called);
  EXPECT_FALSE(result.err.has_error()) << result.err.msg();
  EXPECT_EQ(ExprValue(kValue), result.value);

  // Symbol should match.
  ASSERT_TRUE(result.symbol);
  const Variable* var = result.symbol->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ("present", var->GetFullName());
}

TEST_F(SymbolEvalContextTest, FoundAsynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);
  provider()->set_ip(0x1010);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      fxl::WeakPtr<const ProcessSymbols>(),
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  ValueResult result;
  GetNamedValue(eval_context, "present", kQuitLoop, &result);

  // Should not have been called yet since retrieving the register is
  // asynchronous.
  EXPECT_FALSE(result.called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(result.called);
  EXPECT_FALSE(result.err.has_error()) << result.err.msg();
  EXPECT_EQ(ExprValue(kValue), result.value);

  // Symbol should match.
  ASSERT_TRUE(result.symbol);
  const Variable* var = result.symbol->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ("present", var->GetFullName());
}

// Tests a symbol that's found but couldn't be evaluated (in this case, because
// there's no "register 0" available.
TEST_F(SymbolEvalContextTest, FoundButNotEvaluatable) {
  provider()->set_ip(0x1010);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      fxl::WeakPtr<const ProcessSymbols>(),
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  ValueResult result;
  GetNamedValue(eval_context, "present", kQuitLoop, &result);

  // Running the message loop should complete the callback.
  loop().Run();

  // The value should be not found.
  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.err.has_error());
  EXPECT_EQ(ExprValue(), result.value);

  // The symbol should still have been found even though the value could not
  // be computed.
  ASSERT_TRUE(result.symbol);
  const Variable* var = result.symbol->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ("present", var->GetFullName());
}

// Tests finding variables on |this| and subclasses of |this|.
TEST_F(SymbolEvalContextTest, FoundThis) {
  auto int32_type = MakeInt32Type();
  auto derived = MakeDerivedClassPair(
      DwarfTag::kClassType, "Base", {{"b1", int32_type}, {"b2", int32_type}},
      "Derived", {{"d1", int32_type}, {"d2", int32_type}});

  auto derived_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType,
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
  provider()->AddRegisterValue(kDWARFReg0ID, false, kObjectAddr);
  auto this_var = MakeVariableForTest(
      "this", derived_ptr, 0x1000, 0x2000,
      {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});

  // Make a function with a parameter / object pointer to Derived (this will be
  // like a member function on Derived).
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_parameters({LazySymbol(this_var)});
  function->set_object_pointer(LazySymbol(this_var));

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      fxl::WeakPtr<const ProcessSymbols>(),
      SymbolContext::ForRelativeAddresses(), provider(), function);
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  // First get d2 on the derived class. "this" should be implicit.
  ValueResult result_d2;
  GetNamedValue(eval_context, "d2", kQuitLoop, &result_d2);

  // Should not have been called yet since retrieving the register is
  // asynchronous.
  EXPECT_FALSE(result_d2.called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(result_d2.called);
  EXPECT_FALSE(result_d2.err.has_error()) << result_d2.err.msg();
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(kD2)), result_d2.value);

  // Now get b2 on the base class, it should implicitly find it on "this"
  // and then check the base class.
  ValueResult result_b2;
  GetNamedValue(eval_context, "b2", kQuitLoop, &result_b2);

  EXPECT_FALSE(result_b2.called);
  loop().Run();
  EXPECT_TRUE(result_b2.called);
  EXPECT_FALSE(result_b2.err.has_error()) << result_b2.err.msg();
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(kB2)), result_b2.value);

  // Symbol should match.
  ASSERT_TRUE(result_b2.symbol);
  const DataMember* dm = result_b2.symbol->AsDataMember();
  ASSERT_TRUE(dm);
  EXPECT_EQ("b2", dm->GetFullName());
}

// This is a larger test that runs the EvalContext through ExprNode.Eval.
TEST_F(SymbolEvalContextTest, NodeIntegation) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);
  provider()->set_ip(0x1010);

  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      fxl::WeakPtr<const ProcessSymbols>(),
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  // Look up an identifier that's not present.
  auto present = fxl::MakeRefCounted<IdentifierExprNode>(
      ExprToken(ExprTokenType::kName, "present", 0));
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

TEST_F(SymbolEvalContextTest, RegisterByName) {
  ASSERT_EQ(debug_ipc::Arch::kArm64, provider()->GetArch());

  constexpr uint64_t kRegValue = 0xdeadb33f;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kRegValue);
  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      fxl::WeakPtr<const ProcessSymbols>(),
      SymbolContext::ForRelativeAddresses(), provider(), MakeCodeBlock());
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  // We've defined no variables*, so this should fall back and give us the
  // register by name.   *(Except "present" which MakeCodeBlock defines).
  ValueResult reg;
  GetNamedValue(eval_context, "x0", kQuitLoop, &reg);

  // Should not have been called yet since retrieving the register is
  // asynchronous.
  EXPECT_FALSE(reg.called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(reg.called);
  EXPECT_FALSE(reg.err.has_error()) << reg.err.msg();
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(kRegValue)), reg.value);
}

TEST_F(SymbolEvalContextTest, RegisterShadowed) {
  constexpr uint64_t kRegValue = 0xdeadb33f;
  constexpr uint64_t kVarValue = 0xf00db4be;

  auto shadow_var = MakeUint64VariableForTest(
      "x0", 0x1000, 0x2000,
      {llvm::dwarf::DW_OP_reg1, llvm::dwarf::DW_OP_stack_value});

  auto block = MakeCodeBlock();
  block->set_variables({LazySymbol(std::move(shadow_var))});

  provider()->set_ip(0x1000);
  provider()->AddRegisterValue(kDWARFReg0ID, false, kRegValue);
  provider()->AddRegisterValue(kDWARFReg1ID, false, kVarValue);
  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      fxl::WeakPtr<const ProcessSymbols>(),
      SymbolContext::ForRelativeAddresses(), provider(), block);
  fxl::RefPtr<ExprEvalContext> eval_context(context);

  // This should just look up our variable, x0, which is in the register x1. If
  // It looks up the register x0 something has gone very wrong.
  ValueResult val;
  GetNamedValue(eval_context, "x0", kQuitLoop, &val);

  // Should not have been called yet since retrieving the register is
  // asynchronous.
  EXPECT_FALSE(val.called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(val.called);
  EXPECT_FALSE(val.err.has_error()) << val.err.msg();
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(kVarValue)), val.value);
}

}  // namespace zxdb
