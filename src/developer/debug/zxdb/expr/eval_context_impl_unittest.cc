// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_context_impl.h"

#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
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

class EvalContextImplTest : public TestWithLoop {
 public:
  EvalContextImplTest() : provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) {}

  DwarfExprEval& eval() { return eval_; }
  fxl::RefPtr<MockSymbolDataProvider>& provider() { return provider_; }

  fxl::RefPtr<CodeBlock> MakeCodeBlock() {
    auto block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);

    // Declare a variable in this code block stored in register 0.
    auto variable =
        MakeUint64VariableForTest(kPresentVarName, kBeginValidRange, kEndValidRange,
                                  {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});
    block->set_variables({LazySymbol(std::move(variable))});

    // TODO(brettw) this needs a type. Currently this test is very simple and only outputs internal
    // ints.
    return block;
  }

  // Returns an evaluation context for a code block. If the code block is null, a default one will
  // be created with MakeCodeBlock().
  fxl::RefPtr<EvalContext> MakeEvalContext(fxl::RefPtr<CodeBlock> code_block = nullptr) {
    return fxl::MakeRefCounted<EvalContextImpl>(fxl::WeakPtr<const ProcessSymbols>(),
                                                SymbolContext::ForRelativeAddresses(), provider(),
                                                code_block ? code_block : MakeCodeBlock());
  }

 private:
  DwarfExprEval eval_;
  fxl::RefPtr<MockSymbolDataProvider> provider_;
};

struct ValueResult {
  ValueResult() : value(ExprValue()) {}

  bool called = false;  // Set when the callback is issued.
  ErrOrValue value;
  fxl::RefPtr<Symbol> symbol;
};

// Indicates whether GetNamedValue should exit the message loop when the callback is issued.
// Synchronous results don't need this.
enum GetValueAsync { kQuitLoop, kSynchronous };

// Wrapper around eval_context->GetNamedValue that places the callback parameters into a struct. It
// makes the callsites cleaner.
void GetNamedValue(const fxl::RefPtr<EvalContext>& eval_context, const std::string& name,
                   GetValueAsync async, ValueResult* result) {
  ParsedIdentifier ident;
  Err err = ExprParser::ParseIdentifier(name, &ident);
  ASSERT_FALSE(err.has_error());

  eval_context->GetNamedValue(ident, [result, async](ErrOrValue value, fxl::RefPtr<Symbol> symbol) {
    result->called = true;
    result->value = std::move(value);
    result->symbol = std::move(symbol);
    if (async == kQuitLoop)
      debug_ipc::MessageLoop::Current()->QuitNow();
  });
}

// Sync wrapper around GetVariableValue().
void GetVariableValue(const fxl::RefPtr<EvalContext>& eval_context, fxl::RefPtr<Variable> variable,
                      GetValueAsync async, ValueResult* result) {
  eval_context->GetVariableValue(variable,
                                 [result, async](ErrOrValue value, fxl::RefPtr<Symbol> symbol) {
                                   result->called = true;
                                   result->value = std::move(value);
                                   result->symbol = std::move(symbol);
                                   if (async == kQuitLoop)
                                     debug_ipc::MessageLoop::Current()->QuitNow();
                                 });
}

const debug_ipc::RegisterID kDWARFReg0ID = debug_ipc::RegisterID::kARMv8_x0;
const debug_ipc::RegisterID kDWARFReg1ID = debug_ipc::RegisterID::kARMv8_x1;

}  // namespace

TEST_F(EvalContextImplTest, NotFoundSynchronous) {
  provider()->set_ip(0x1010);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, "not_present", kSynchronous, &result);

  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.value.has_error());
  EXPECT_FALSE(result.symbol);
}

TEST_F(EvalContextImplTest, FoundSynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->set_ip(0x1010);
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, kPresentVarName, kSynchronous, &result);

  EXPECT_TRUE(result.called);
  EXPECT_FALSE(result.value.has_error()) << result.value.err().msg();
  EXPECT_EQ(ExprValue(kValue), result.value.value());

  // Symbol should match.
  ASSERT_TRUE(result.symbol);
  const Variable* var = result.symbol->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ(kPresentVarName, var->GetFullName());
}

TEST_F(EvalContextImplTest, FoundAsynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);
  provider()->set_ip(0x1010);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, kPresentVarName, kQuitLoop, &result);

  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(result.called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(result.called);
  EXPECT_FALSE(result.value.has_error()) << result.value.err().msg();
  EXPECT_EQ(ExprValue(kValue), result.value.value());

  // Symbol should match.
  ASSERT_TRUE(result.symbol);
  const Variable* var = result.symbol->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ(kPresentVarName, var->GetFullName());
}

// Tests a symbol that's found but couldn't be evaluated (in this case, because there's no "register
// 0" available.
TEST_F(EvalContextImplTest, FoundButNotEvaluatable) {
  provider()->set_ip(0x1010);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, kPresentVarName, kSynchronous, &result);

  // The value should be not found and this should be known synchronously.
  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.value.has_error());

  // The symbol should still have been found even though the value could not be computed.
  ASSERT_TRUE(result.symbol);
  const Variable* var = result.symbol->AsVariable();
  ASSERT_TRUE(var);
  EXPECT_EQ(kPresentVarName, var->GetFullName());

  // Prevent leak by processing pending messages. The symbol eval context currently deletes the
  // DwarfExprEval on a PostTask().
  loop().PostTask(FROM_HERE, [loop = &loop()]() { loop->QuitNow(); });
  loop().Run();
}

// Tests finding variables on |this| and subclasses of |this|.
TEST_F(EvalContextImplTest, FoundThis) {
  auto int32_type = MakeInt32Type();
  auto derived =
      MakeDerivedClassPair(DwarfTag::kClassType, "Base", {{"b1", int32_type}, {"b2", int32_type}},
                           "Derived", {{"d1", int32_type}, {"d2", int32_type}});

  auto derived_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, derived);

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
  auto this_var = MakeVariableForTest("this", derived_ptr, kBeginValidRange, kEndValidRange,
                                      {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});

  // Make a function with a parameter / object pointer to Derived (this will be like a member
  // function on Derived).
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_parameters({LazySymbol(this_var)});
  function->set_object_pointer(this_var);

  auto context = MakeEvalContext(function);

  // First get d2 on the derived class. "this" should be implicit.
  ValueResult result_d2;
  GetNamedValue(context, "d2", kQuitLoop, &result_d2);

  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(result_d2.called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(result_d2.called);
  EXPECT_FALSE(result_d2.value.has_error()) << result_d2.value.err().msg();
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(kD2)), result_d2.value.value());

  // Now get b2 on the base class, it should implicitly find it on "this" and then check the base
  // class.
  ValueResult result_b2;
  GetNamedValue(context, "b2", kQuitLoop, &result_b2);

  EXPECT_FALSE(result_b2.called);
  loop().Run();
  EXPECT_TRUE(result_b2.called);
  EXPECT_FALSE(result_b2.value.has_error()) << result_b2.value.err().msg();
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(kB2)), result_b2.value.value());

  // Symbol should match.
  ASSERT_TRUE(result_b2.symbol);
  const DataMember* dm = result_b2.symbol->AsDataMember();
  ASSERT_TRUE(dm);
  EXPECT_EQ("b2", dm->GetFullName());
}

// Tests a variable lookup that has the IP out of range of the variable's validity.
TEST_F(EvalContextImplTest, RangeMiss) {
  // Set up a valid register for the variable. A missing register shouldn't be why it fails to be
  // found.
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);
  provider()->set_ip(kEndValidRange + 0x10);

  ValueResult result;
  GetNamedValue(MakeEvalContext(), kPresentVarName, kSynchronous, &result);
  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.value.has_error());
  EXPECT_EQ(ErrType::kOptimizedOut, result.value.err().type());
}

// Tests the DWARF expression evaluation failing (empty expression).
TEST_F(EvalContextImplTest, DwarfEvalFailure) {
  const char kEmptyExprVarName[] = "empty_expr";
  provider()->set_ip(kBeginValidRange);

  auto var = MakeUint64VariableForTest(kEmptyExprVarName, kBeginValidRange, kEndValidRange, {});

  auto block = MakeCodeBlock();
  block->set_variables({LazySymbol(std::move(var))});

  ValueResult result;
  GetNamedValue(MakeEvalContext(block), kEmptyExprVarName, kSynchronous, &result);
  EXPECT_TRUE(result.called);
  ASSERT_TRUE(result.value.has_error());
  EXPECT_EQ("DWARF expression produced no results.", result.value.err().msg());
}

// Tests asynchronously reading an integer from memory. This also tests interleaved execution of
// multiple requests by having a resolution miss request execute while the memory request is
// pending.
TEST_F(EvalContextImplTest, IntOnStack) {
  // Define a 4-byte integer (=0x12345678) at location bp+8
  constexpr int32_t kValue = 0x12345678;

  constexpr uint8_t kOffset = 8;
  auto type = MakeInt32Type();
  auto var = MakeUint64VariableForTest("i", 0, 0, {llvm::dwarf::DW_OP_fbreg, kOffset});
  var->set_type(type);

  constexpr uint64_t kBp = 0x1000;
  provider()->set_bp(kBp);
  provider()->set_ip(0x1000);
  provider()->AddMemory(kBp + kOffset, {0x78, 0x56, 0x34, 0x12});

  auto context = MakeEvalContext();

  ValueResult result1;
  GetVariableValue(context, var, kQuitLoop, &result1);

  // Should be run async since it requests memory.
  EXPECT_FALSE(result1.called);
  EXPECT_FALSE(result1.value.has_error()) << result1.value.err().msg();

  // Before running the loop and receiving the memory, start a new request, this one will fail
  // synchronously due to a range miss.
  auto rangemiss =
      MakeUint64VariableForTest("rangemiss", 0x6000, 0x7000, {llvm::dwarf::DW_OP_reg0});
  ValueResult result2;
  GetVariableValue(context, rangemiss, kSynchronous, &result2);
  EXPECT_TRUE(result2.called);
  EXPECT_TRUE(result2.value.err().has_error());
  EXPECT_EQ(ErrType::kOptimizedOut, result2.value.err().type());

  // Now let the first request complete.
  loop().Run();
  EXPECT_TRUE(result1.called);
  ASSERT_FALSE(result1.value.has_error()) << result1.value.err().msg();
  EXPECT_EQ(ExprValue(kValue), result1.value.value());
}

// Tests that externs are resolved by GetVariableValue(). This requires using the index.
TEST_F(EvalContextImplTest, ExternVariable) {
  // Offset from beginning of the module of the data.
  constexpr uint8_t kRelativeValAddress = 0x99;
  const char kValName[] = "val";

  // The non-extern declaration for the variable (0, 0 means always valid). The little-endian
  // module-relative address follows DW_OP_addr in the expression.
  auto real_variable = MakeUint64VariableForTest(
      kValName, 0, 0, {llvm::dwarf::DW_OP_addr, kRelativeValAddress, 0, 0, 0, 0, 0, 0, 0});

  // A reference to the same variable, marked "external" with no location.
  auto extern_variable = fxl::MakeRefCounted<Variable>(DwarfTag::kVariable);
  extern_variable->set_assigned_name(kValName);
  extern_variable->set_is_external(true);
  extern_variable->set_type(MakeUint64Type());

  constexpr uint64_t kLoadAddress = 0x1000000;
  constexpr uint64_t kAbsoluteValAddress = kLoadAddress + kRelativeValAddress;

  // Need to have a module for the variable to be relative to and to have an index.
  ProcessSymbolsTestSetup setup;
  auto module_symbols = fxl::MakeRefCounted<MockModuleSymbols>("mod.so");
  SymbolContext symbol_context(kLoadAddress);
  setup.InjectModule("mod1", "1234", kLoadAddress, module_symbols);

  // Index the non-extern variable.
  auto& root = module_symbols->index().root();  // Root of the index for module.
  TestIndexedSymbol indexed_def(module_symbols.get(), &root, kValName, real_variable);

  // Set the value for the non-extern variable in the mocked memory.
  constexpr uint64_t kValValue = 0x0102030405060708;
  provider()->AddMemory(kAbsoluteValAddress, {8, 7, 6, 5, 4, 3, 2, 1});

  auto context = fxl::MakeRefCounted<EvalContextImpl>(setup.process().GetWeakPtr(), symbol_context,
                                                      provider(), MakeCodeBlock());

  // Resolving the extern variable should give the value that the non-extern one points to.
  ValueResult result;
  GetVariableValue(context, extern_variable, GetValueAsync::kQuitLoop, &result);
  loop().Run();
  ASSERT_TRUE(result.called);
  ASSERT_TRUE(result.value.ok());
  EXPECT_EQ(ExprValue(kValValue), result.value.value());
  EXPECT_EQ(static_cast<const Symbol*>(real_variable.get()), result.symbol.get());
}

// This is a larger test that runs the EvalContext through ExprNode.Eval.
TEST_F(EvalContextImplTest, NodeIntegation) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);
  provider()->set_ip(kBeginValidRange + 0x10);

  auto context = MakeEvalContext();

  // Look up an identifier that's not present.
  auto present = fxl::MakeRefCounted<IdentifierExprNode>(kPresentVarName);
  bool called = false;
  ExprValue out_value;
  present->Eval(context, [&called, &out_value](ErrOrValue value) {
    called = true;
    EXPECT_FALSE(value.has_error());
    out_value = value.take_value();
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(called);

  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_EQ(ExprValue(kValue), out_value);
}

TEST_F(EvalContextImplTest, RegisterByName) {
  ASSERT_EQ(debug_ipc::Arch::kArm64, provider()->GetArch());

  constexpr uint64_t kRegValue = 0xdeadb33f;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kRegValue);
  auto context = MakeEvalContext();

  // We've defined no variables*, so this should fall back and give us the register by name.
  // *(Except kPresentVarName which MakeCodeBlock defines).
  ValueResult reg;
  GetNamedValue(context, "x0", kQuitLoop, &reg);

  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(reg.called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(reg.called);
  EXPECT_FALSE(reg.value.has_error()) << reg.value.err().msg();
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(kRegValue)), reg.value.value());
}

TEST_F(EvalContextImplTest, RegisterShadowed) {
  constexpr uint64_t kRegValue = 0xdeadb33f;
  constexpr uint64_t kVarValue = 0xf00db4be;

  auto shadow_var =
      MakeUint64VariableForTest("x0", kBeginValidRange, kEndValidRange,
                                {llvm::dwarf::DW_OP_reg1, llvm::dwarf::DW_OP_stack_value});

  auto block = MakeCodeBlock();
  block->set_variables({LazySymbol(std::move(shadow_var))});

  provider()->set_ip(kBeginValidRange);
  provider()->AddRegisterValue(kDWARFReg0ID, false, kRegValue);
  provider()->AddRegisterValue(kDWARFReg1ID, false, kVarValue);
  auto context = MakeEvalContext(block);

  // This should just look up our variable, x0, which is in the register x1. If It looks up the
  // register x0 something has gone very wrong.
  ValueResult val;
  GetNamedValue(context, "x0", kQuitLoop, &val);

  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(val.called);

  // Running the message loop should complete the callback.
  loop().Run();
  EXPECT_TRUE(val.called);
  ASSERT_FALSE(val.value.has_error()) << val.value.err().msg();
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(kVarValue)), val.value.value());
}

// Also tests ResolveForwardDefinition().
TEST_F(EvalContextImplTest, GetConcreteType) {
  ProcessSymbolsTestSetup setup;
  auto module_symbols = fxl::MakeRefCounted<MockModuleSymbols>("mod.so");

  constexpr uint64_t kLoadAddress = 0x1000000;
  SymbolContext symbol_context(kLoadAddress);
  setup.InjectModule("mod1", "1234", kLoadAddress, module_symbols);

  auto& root = module_symbols->index().root();  // Root of the index for module 1.

  const char kMyStructName[] = "MyStruct";

  // Make a forward declaration. It has the declaration flag set and no members or size.
  auto forward_decl = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  forward_decl->set_assigned_name(kMyStructName);
  forward_decl->set_is_declaration(true);

  // A const modification of the forward declaration.
  auto const_forward_decl = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, forward_decl);

  // Make a symbol context.
  auto context = fxl::MakeRefCounted<EvalContextImpl>(setup.process().GetWeakPtr(), symbol_context,
                                                      provider(), fxl::RefPtr<CodeBlock>());

  // Resolving the const forward-defined value gives the non-const version.
  auto result_type = context->GetConcreteType(const_forward_decl.get());
  EXPECT_EQ(forward_decl.get(), result_type.get());

  // Make a definition for the type. It has one 32-bit data member.
  auto def = MakeCollectionType(DwarfTag::kStructureType, kMyStructName, {{"a", MakeInt32Type()}});

  // Index the declaration of the type.
  TestIndexedSymbol indexed_def(module_symbols.get(), &root, kMyStructName, def);

  // Now that the index exists for the type, both the const and non-const declarations should
  // resolve to the full definition.
  result_type = context->GetConcreteType(forward_decl.get());
  EXPECT_EQ(def.get(), result_type.get());
  result_type = context->GetConcreteType(const_forward_decl.get());
  EXPECT_EQ(def.get(), result_type.get());
}

}  // namespace zxdb
