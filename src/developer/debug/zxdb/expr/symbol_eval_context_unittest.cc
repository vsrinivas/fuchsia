// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/symbol_eval_context.h"

#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/index_test_support.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"

namespace zxdb {

namespace {

// The range of IP addresses where the "present" variable is valid.
constexpr uint64_t kBeginValidRange = 0x1000;
constexpr uint64_t kEndValidRange = 0x2000;

const char kPresentVarName[] = "present";

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
        kPresentVarName, kBeginValidRange, kEndValidRange,
        {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});
    block->set_variables({LazySymbol(std::move(variable))});

    // TODO(brettw) this needs a type. Currently this test is very simple and
    // only outputs internal ints.
    return block;
  }

  // Returns an evaluation context for a code block. If the code block is null,
  // a default one will be created with MakeCodeBlock().
  fxl::RefPtr<ExprEvalContext> MakeEvalContext(
      fxl::RefPtr<CodeBlock> code_block = nullptr) {
    return fxl::MakeRefCounted<SymbolEvalContext>(
        fxl::WeakPtr<const ProcessSymbols>(),
        SymbolContext::ForRelativeAddresses(), provider(),
        code_block ? code_block : MakeCodeBlock());
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
enum GetValueAsync { kQuitLoop, kSynchronous };

// Wrapper around eval_context->GetNamedValue that places the callback
// parameters into a struct. It makes the callsites cleaner.
void GetNamedValue(const fxl::RefPtr<ExprEvalContext>& eval_context,
                   const std::string& name, GetValueAsync async,
                   ValueResult* result) {
  ParsedIdentifier ident;
  Err err = ExprParser::ParseIdentifier(name, &ident);
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

// Sync wrapper around GetVariableValue().
void GetVariableValue(const fxl::RefPtr<ExprEvalContext>& eval_context,
                      fxl::RefPtr<Variable> variable, GetValueAsync async,
                      ValueResult* result) {
  eval_context->GetVariableValue(
      variable, [result, async](const Err& err, fxl::RefPtr<Symbol> symbol,
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

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, "not_present", kSynchronous, &result);

  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.err.has_error());
  EXPECT_EQ(ExprValue(), result.value);
  EXPECT_FALSE(result.symbol);
}

TEST_F(SymbolEvalContextTest, FoundSynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->set_ip(0x1010);
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, kPresentVarName, kSynchronous, &result);

  EXPECT_TRUE(result.called);
  EXPECT_FALSE(result.err.has_error()) << result.err.msg();
  EXPECT_EQ(ExprValue(kValue), result.value);

  // Symbol should match.
  ASSERT_TRUE(result.symbol);
  const Variable* var = result.symbol->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ(kPresentVarName, var->GetFullName());
}

TEST_F(SymbolEvalContextTest, FoundAsynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);
  provider()->set_ip(0x1010);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, kPresentVarName, kQuitLoop, &result);

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
  EXPECT_EQ(kPresentVarName, var->GetFullName());
}

// Tests a symbol that's found but couldn't be evaluated (in this case, because
// there's no "register 0" available.
TEST_F(SymbolEvalContextTest, FoundButNotEvaluatable) {
  provider()->set_ip(0x1010);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, kPresentVarName, kSynchronous, &result);

  // The value should be not found and this should be known synchronously.
  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.err.has_error());
  EXPECT_EQ(ExprValue(), result.value);

  // The symbol should still have been found even though the value could not
  // be computed.
  ASSERT_TRUE(result.symbol);
  const Variable* var = result.symbol->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ(kPresentVarName, var->GetFullName());

  // Prevent leak by processing pending messages. The symbol eval context
  // currently deletes the DwarfExprEval on a PostTask().
  loop().PostTask(FROM_HERE, [loop = &loop()]() { loop->QuitNow(); });
  loop().Run();
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
  provider()->set_ip(kBeginValidRange);
  provider()->AddRegisterValue(kDWARFReg0ID, false, kObjectAddr);
  auto this_var = MakeVariableForTest(
      "this", derived_ptr, kBeginValidRange, kEndValidRange,
      {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});

  // Make a function with a parameter / object pointer to Derived (this will be
  // like a member function on Derived).
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_parameters({LazySymbol(this_var)});
  function->set_object_pointer(LazySymbol(this_var));

  auto context = MakeEvalContext(function);

  // First get d2 on the derived class. "this" should be implicit.
  ValueResult result_d2;
  GetNamedValue(context, "d2", kQuitLoop, &result_d2);

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
  GetNamedValue(context, "b2", kQuitLoop, &result_b2);

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

// Tests a variable lookup that has the IP out of range of the variable's
// validity.
TEST_F(SymbolEvalContextTest, RangeMiss) {
  // Set up a valid register for the variable. A missing register shouldn't be
  // why it fails to be found.
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);
  provider()->set_ip(kEndValidRange + 0x10);

  ValueResult result;
  GetNamedValue(MakeEvalContext(), kPresentVarName, kSynchronous, &result);
  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.err.has_error());
  EXPECT_EQ(ErrType::kOptimizedOut, result.err.type());
  EXPECT_EQ(ExprValue(), result.value);
}

// Tests the DWARF expression evaluation failing (empty expression).
TEST_F(SymbolEvalContextTest, DwarfEvalFailure) {
  const char kEmptyExprVarName[] = "empty_expr";
  provider()->set_ip(kBeginValidRange);

  auto var = MakeUint64VariableForTest(kEmptyExprVarName, kBeginValidRange,
                                       kEndValidRange, {});

  auto block = MakeCodeBlock();
  block->set_variables({LazySymbol(std::move(var))});

  ValueResult result;
  GetNamedValue(MakeEvalContext(block), kEmptyExprVarName, kSynchronous,
                &result);
  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.err.has_error());
  EXPECT_EQ("DWARF expression produced no results.", result.err.msg());
  EXPECT_EQ(ExprValue(), result.value);
}

// Tests asynchronously reading an integer from memory. This also tests
// interleaved execution of multiple requests by having a resolution miss
// request execute while the memory request is pending.
TEST_F(SymbolEvalContextTest, IntOnStack) {
  // Define a 4-byte integer (=0x12345678) at location bp+8
  constexpr int32_t kValue = 0x12345678;

  constexpr uint8_t kOffset = 8;
  auto type = MakeInt32Type();
  auto var =
      MakeUint64VariableForTest("i", 0, 0, {llvm::dwarf::DW_OP_fbreg, kOffset});
  var->set_type(LazySymbol(type));

  constexpr uint64_t kBp = 0x1000;
  provider()->set_bp(kBp);
  provider()->set_ip(0x1000);
  provider()->AddMemory(kBp + kOffset, {0x78, 0x56, 0x34, 0x12});

  auto context = MakeEvalContext();

  ValueResult result1;
  GetVariableValue(context, var, kQuitLoop, &result1);

  // Should be run async since it requests memory.
  EXPECT_FALSE(result1.called);
  EXPECT_FALSE(result1.err.has_error()) << result1.err.msg();

  // Before running the loop and receiving the memory, start a new request,
  // this one will fail synchronously due to a range miss.
  auto rangemiss = MakeUint64VariableForTest("rangemiss", 0x6000, 0x7000,
                                             {llvm::dwarf::DW_OP_reg0});
  ValueResult result2;
  GetVariableValue(context, rangemiss, kSynchronous, &result2);
  EXPECT_TRUE(result2.called);
  EXPECT_TRUE(result2.err.has_error());
  EXPECT_EQ(ErrType::kOptimizedOut, result2.err.type());
  EXPECT_EQ(ExprValue(), result2.value);

  // Now let the first request complete.
  loop().Run();
  EXPECT_TRUE(result1.called);
  EXPECT_FALSE(result1.err.has_error()) << result1.err.msg();
  EXPECT_EQ(ExprValue(kValue), result1.value);
}

// This is a larger test that runs the EvalContext through ExprNode.Eval.
TEST_F(SymbolEvalContextTest, NodeIntegation) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);
  provider()->set_ip(kBeginValidRange + 0x10);

  auto context = MakeEvalContext();

  // Look up an identifier that's not present.
  auto present = fxl::MakeRefCounted<IdentifierExprNode>(kPresentVarName);
  bool called = false;
  Err out_err;
  ExprValue out_value;
  present->Eval(context, [&called, &out_err, &out_value](const Err& err,
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
  auto context = MakeEvalContext();

  // We've defined no variables*, so this should fall back and give us the
  // register by name.   *(Except kPresentVarName which MakeCodeBlock defines).
  ValueResult reg;
  GetNamedValue(context, "x0", kQuitLoop, &reg);

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
      "x0", kBeginValidRange, kEndValidRange,
      {llvm::dwarf::DW_OP_reg1, llvm::dwarf::DW_OP_stack_value});

  auto block = MakeCodeBlock();
  block->set_variables({LazySymbol(std::move(shadow_var))});

  provider()->set_ip(kBeginValidRange);
  provider()->AddRegisterValue(kDWARFReg0ID, false, kRegValue);
  provider()->AddRegisterValue(kDWARFReg1ID, false, kVarValue);
  auto context = MakeEvalContext(block);

  // This should just look up our variable, x0, which is in the register x1. If
  // It looks up the register x0 something has gone very wrong.
  ValueResult val;
  GetNamedValue(context, "x0", kQuitLoop, &val);

  // Should not have been called yet since retrieving the register is
  // asynchronous.
  EXPECT_FALSE(val.called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(val.called);
  EXPECT_FALSE(val.err.has_error()) << val.err.msg();
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(kVarValue)), val.value);
}

// Also tests ResolveForwardDefinition().
TEST_F(SymbolEvalContextTest, GetConcreteType) {
  ProcessSymbolsTestSetup setup;
  auto mod_ref = std::make_unique<MockModuleSymbols>("mod.so");
  MockModuleSymbols* mod = mod_ref.get();  // Save for later.

  constexpr uint64_t kLoadAddress = 0x1000000;
  SymbolContext symbol_context(kLoadAddress);
  setup.InjectModule("mod1", "1234", kLoadAddress, std::move(mod_ref));

  auto& root = mod->index().root();  // Root of the index for module 1.

  const char kMyStructName[] = "MyStruct";

  // Make a forward definition for MyStruct. Is has the declaration flag set
  // and no members or size.
  auto forward_def = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  forward_def->set_assigned_name(kMyStructName);
  forward_def->set_is_declaration(true);

  // A const modification of the forward definition.
  auto const_forward_def = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kConstType, LazySymbol(forward_def));

  // Make a symbol context.
  auto context = fxl::MakeRefCounted<SymbolEvalContext>(
      setup.process().GetWeakPtr(), symbol_context, provider(),
      fxl::RefPtr<CodeBlock>());

  // Resolving the const forward-defined value gives the non-const version.
  auto result_type = context->GetConcreteType(const_forward_def.get());
  EXPECT_EQ(forward_def.get(), result_type.get());

  // Make a definition for the type. It has one 32-bit data member.
  auto decl = MakeCollectionType(DwarfTag::kStructureType, kMyStructName,
                                 {{"a", MakeInt32Type()}});

  // Index the declaration of the type.
  TestIndexedSymbol indexed_decl(mod, &root, kMyStructName, decl);

  // Now that the index exists for the type, both the const and non-const
  // declarations should resolve to the full definition.
  result_type = context->GetConcreteType(forward_def.get());
  EXPECT_EQ(decl.get(), result_type.get());
  result_type = context->GetConcreteType(const_forward_def.get());
  EXPECT_EQ(decl.get(), result_type.get());
}

}  // namespace zxdb
