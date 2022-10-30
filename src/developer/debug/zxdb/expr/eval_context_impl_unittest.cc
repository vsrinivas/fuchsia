// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/eval_context_impl.h"

#include <gtest/gtest.h>

#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/test_with_loop.h"
#include "src/developer/debug/zxdb/expr/abi_null.h"
#include "src/developer/debug/zxdb/expr/expr_node.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/expr/vm_exec.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/code_block.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/dwarf_expr_eval.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/index_test_support.h"
#include "src/developer/debug/zxdb/symbols/inherited_from.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_factory.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
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
  EvalContextImplTest() {}

  DwarfExprEval& eval() { return eval_; }
  fxl::RefPtr<MockSymbolDataProvider>& provider() { return provider_; }

  fxl::RefPtr<CodeBlock> MakeCodeBlock() {
    auto block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);

    // Declare a variable in this code block stored in register 0.
    auto variable = MakeUint64VariableForTest(
        kPresentVarName, kBeginValidRange, kEndValidRange,
        DwarfExpr({llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value}));
    block->set_variables({LazySymbol(std::move(variable))});

    // TODO(brettw) this needs a type. Currently this test is very simple and only outputs internal
    // ints.
    return block;
  }

  // Returns an evaluation context for a code block. If the code block is null, a default one will
  // be created with MakeCodeBlock().
  fxl::RefPtr<EvalContext> MakeEvalContext(ExprLanguage lang = ExprLanguage::kC,
                                           fxl::RefPtr<CodeBlock> code_block = nullptr) {
    return fxl::MakeRefCounted<EvalContextImpl>(std::make_shared<AbiNull>(),
                                                fxl::WeakPtr<const ProcessSymbols>(), provider(),
                                                lang, code_block ? code_block : MakeCodeBlock());
  }

 private:
  SymbolContext symbol_context_ = SymbolContext(ProcessSymbolsTestSetup::kDefaultLoadAddress);
  fxl::RefPtr<MockSymbolDataProvider> provider_ = fxl::MakeRefCounted<MockSymbolDataProvider>();

  // We do not test expressions that refer to DIEs (for type information) so can supply an empty
  // UnitSymbolFactory.
  DwarfExprEval eval_ = DwarfExprEval(UnitSymbolFactory(), provider_, symbol_context_);
};

struct ValueResult {
  ValueResult() : value(ExprValue()) {}

  bool called = false;  // Set when the callback is issued.
  ErrOrValue value;
};

// Wrapper around eval_context->GetNamedValue that places the callback parameters into a struct. It
// makes the callsites cleaner.
void GetNamedValue(const fxl::RefPtr<EvalContext>& eval_context, const std::string& name,
                   ValueResult* result) {
  ParsedIdentifier ident;
  Err err = ExprParser::ParseIdentifier(name, &ident);
  ASSERT_FALSE(err.has_error()) << name;

  eval_context->GetNamedValue(ident, [result](ErrOrValue value) {
    result->called = true;
    result->value = std::move(value);
  });
}

// Sync wrapper around GetVariableValue().
void GetVariableValue(const fxl::RefPtr<EvalContext>& eval_context, fxl::RefPtr<Variable> variable,
                      ValueResult* result) {
  eval_context->GetVariableValue(variable, [result](ErrOrValue value) {
    result->called = true;
    result->value = std::move(value);
  });
}

const debug::RegisterID kDWARFReg0ID = debug::RegisterID::kARMv8_x0;
const debug::RegisterID kDWARFReg1ID = debug::RegisterID::kARMv8_x1;

}  // namespace

TEST_F(EvalContextImplTest, NotFoundSynchronous) {
  provider()->set_ip(0x1010);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, "not_present", &result);

  EXPECT_TRUE(result.called);
  EXPECT_TRUE(result.value.has_error());
}

TEST_F(EvalContextImplTest, FoundSynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->set_ip(0x1010);
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, kPresentVarName, &result);

  EXPECT_TRUE(result.called);
  EXPECT_FALSE(result.value.has_error()) << result.value.err().msg();
  EXPECT_EQ(ExprValue(kValue), result.value.value());
}

TEST_F(EvalContextImplTest, FoundAsynchronous) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);
  provider()->set_ip(0x1010);

  auto context = MakeEvalContext();

  ValueResult result;
  GetNamedValue(context, kPresentVarName, &result);

  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(result.called);

  // Running the message loop should complete the callback.
  loop().RunUntilNoTasks();
  EXPECT_TRUE(result.called);
  EXPECT_FALSE(result.value.has_error()) << result.value.err().msg();
  EXPECT_EQ(ExprValue(kValue), result.value.value());
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
  auto this_var =
      MakeVariableForTest("this", derived_ptr, kBeginValidRange, kEndValidRange,
                          DwarfExpr({llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value}));

  // Make a function with a parameter / object pointer to Derived (this will be like a member
  // function on Derived).
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_parameters({LazySymbol(this_var)});
  function->set_object_pointer(this_var);

  auto context = MakeEvalContext(ExprLanguage::kC, function);

  // First get d2 on the derived class. "this" should be implicit.
  ValueResult result_d2;
  GetNamedValue(context, "d2", &result_d2);

  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(result_d2.called);

  // Running the message loop should complete the callback.
  loop().RunUntilNoTasks();
  EXPECT_TRUE(result_d2.called);
  EXPECT_FALSE(result_d2.value.has_error()) << result_d2.value.err().msg();
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(kD2)), result_d2.value.value());

  // Now get b2 on the base class, it should implicitly find it on "this" and then check the base
  // class.
  ValueResult result_b2;
  GetNamedValue(context, "b2", &result_b2);

  EXPECT_FALSE(result_b2.called);
  loop().RunUntilNoTasks();
  EXPECT_TRUE(result_b2.called);
  EXPECT_FALSE(result_b2.value.has_error()) << result_b2.value.err().msg();
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(kB2)), result_b2.value.value());
}

// Tests a variable lookup that has the IP out of range of the variable's validity.
TEST_F(EvalContextImplTest, RangeMiss) {
  // Set up a valid register for the variable. A missing register shouldn't be why it fails to be
  // found.
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);
  provider()->set_ip(kEndValidRange + 0x10);

  ValueResult result;
  GetNamedValue(MakeEvalContext(), kPresentVarName, &result);
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
  GetNamedValue(MakeEvalContext(ExprLanguage::kC, block), kEmptyExprVarName, &result);
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
  auto var = MakeUint64VariableForTest(
      "i", VariableLocation(DwarfExpr({llvm::dwarf::DW_OP_fbreg, kOffset})));
  var->set_type(type);

  constexpr uint64_t kBp = 0x1000;
  provider()->set_bp(kBp);
  provider()->set_ip(0x1000);
  provider()->AddMemory(kBp + kOffset, {0x78, 0x56, 0x34, 0x12});

  auto context = MakeEvalContext();

  ValueResult result1;
  GetVariableValue(context, var, &result1);

  // Should be run async since it requests memory.
  EXPECT_FALSE(result1.called);
  EXPECT_FALSE(result1.value.has_error()) << result1.value.err().msg();

  // Before running the loop and receiving the memory, start a new request, this one will fail
  // synchronously due to a range miss.
  auto rangemiss =
      MakeUint64VariableForTest("rangemiss", 0x6000, 0x7000, DwarfExpr({llvm::dwarf::DW_OP_reg0}));
  ValueResult result2;
  GetVariableValue(context, rangemiss, &result2);
  EXPECT_TRUE(result2.called);
  EXPECT_TRUE(result2.value.err().has_error());
  EXPECT_EQ(ErrType::kOptimizedOut, result2.value.err().type());

  // Now let the first request complete.
  loop().RunUntilNoTasks();
  EXPECT_TRUE(result1.called);
  ASSERT_FALSE(result1.value.has_error()) << result1.value.err().msg();
  EXPECT_EQ(ExprValue(kValue), result1.value.value());

  // Validate variable source annotation.
  const ExprValueSource& source = result1.value.value().source();
  EXPECT_EQ(ExprValueSource::Type::kMemory, source.type());
  EXPECT_EQ(kBp + kOffset, source.address());
}

// Checks that constant DWARF expressions result in constant variables.
TEST_F(EvalContextImplTest, ConstantVariable) {
  auto type = MakeInt32Type();
  auto var = MakeUint64VariableForTest(
      "i", VariableLocation(DwarfExpr({llvm::dwarf::DW_OP_lit3, llvm::dwarf::DW_OP_stack_value})));
  var->set_type(type);

  ValueResult result;
  GetVariableValue(MakeEvalContext(), var, &result);
  loop().RunUntilNoTasks();

  ASSERT_TRUE(result.called);
  EXPECT_EQ(3, result.value.value().GetAs<int32_t>());
  EXPECT_EQ(ExprValueSource::Type::kConstant, result.value.value().source().type());
}

// Tests that externs are resolved by GetVariableValue(). This requires using the index.
TEST_F(EvalContextImplTest, ExternVariable) {
  // Need to have a module for the variable to be relative to and to have an index.
  ProcessSymbolsTestSetup setup;
  MockModuleSymbols* module_symbols = setup.InjectMockModule();

  // Offset from beginning of the module of the data.
  constexpr uint8_t kRelativeValAddress = 0x99;
  constexpr uint64_t kAbsoluteValAddress =
      ProcessSymbolsTestSetup::kDefaultLoadAddress + kRelativeValAddress;
  const char kValName[] = "val";

  // The non-extern declaration for the variable (0, 0 means always valid). The little-endian
  // module-relative address follows DW_OP_addr in the expression.
  auto real_variable = MakeUint64VariableForTest(
      kValName, VariableLocation(DwarfExpr(
                    {llvm::dwarf::DW_OP_addr, kRelativeValAddress, 0, 0, 0, 0, 0, 0, 0})));

  // The variable needs to have a unit that references the module to provide the symbol context
  // in which to evaluate the location expression. This will convert the kRelativeValAddress to
  // kAbsoluteValAddress.
  auto unit =
      fxl::MakeRefCounted<CompileUnit>(module_symbols->GetWeakPtr(), fxl::RefPtr<DwarfUnit>(),
                                       DwarfLang::kC, "file.cc", std::nullopt);
  SymbolTestParentSetter var_parent(real_variable, unit);

  // A reference to the same variable, marked "external" with no location.
  auto extern_variable = fxl::MakeRefCounted<Variable>(DwarfTag::kVariable);
  extern_variable->set_assigned_name(kValName);
  extern_variable->set_is_external(true);
  extern_variable->set_type(MakeUint64Type());

  // Index the non-extern variable.
  TestIndexedSymbol indexed_def(module_symbols, &module_symbols->index().root(), kValName,
                                real_variable);

  // Set the value for the non-extern variable in the mocked memory.
  constexpr uint64_t kValValue = 0x0102030405060708;
  provider()->AddMemory(kAbsoluteValAddress, {8, 7, 6, 5, 4, 3, 2, 1});

  auto context = fxl::MakeRefCounted<EvalContextImpl>(std::make_shared<AbiNull>(),
                                                      setup.process().GetWeakPtr(), provider(),
                                                      ExprLanguage::kC, MakeCodeBlock());

  // Resolving the extern variable should give the value that the non-extern one points to.
  ValueResult result;
  GetVariableValue(context, extern_variable, &result);
  loop().RunUntilNoTasks();
  ASSERT_TRUE(result.called);
  ASSERT_TRUE(result.value.ok()) << result.value.err().msg();
  EXPECT_EQ(ExprValue(kValValue), result.value.value());
}

// This is a larger test that runs the EvalContext through ExprNode.Eval.
TEST_F(EvalContextImplTest, NodeIntegation) {
  constexpr uint64_t kValue = 12345678;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kValue);
  provider()->set_ip(kBeginValidRange + 0x10);

  auto context = MakeEvalContext();

  VmStream stream;
  auto present = fxl::MakeRefCounted<IdentifierExprNode>(kPresentVarName);
  present->EmitBytecode(stream);

  ExprValue out_value;
  bool called = false;
  VmExec(context, std::move(stream), [&called, &out_value](ErrOrValue value) {
    called = true;
    EXPECT_FALSE(value.has_error());
    out_value = value.take_value();
  });
  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(called);

  loop().RunUntilNoTasks();
  EXPECT_TRUE(called);
  EXPECT_EQ(ExprValue(kValue), out_value);
}

TEST_F(EvalContextImplTest, RegisterByName) {
  ASSERT_EQ(debug::Arch::kArm64, provider()->GetArch());

  // Integer value.
  constexpr uint64_t kRegValue = 0xdeadb33f;
  provider()->AddRegisterValue(kDWARFReg0ID, false, kRegValue);

  auto c_context = MakeEvalContext(ExprLanguage::kC);

  // We've defined no variables*, so this should fall back and give us the register by name.
  // *(Except kPresentVarName which MakeCodeBlock defines).
  ValueResult reg;
  GetNamedValue(c_context, "x0", &reg);

  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(reg.called);

  // Running the message loop should complete the callback.
  loop().RunUntilNoTasks();
  EXPECT_TRUE(reg.called);
  EXPECT_FALSE(reg.value.has_error()) << reg.value.err().msg();

  EXPECT_EQ(kRegValue, reg.value.value().GetAs<uint64_t>());
  EXPECT_EQ("uint64_t", reg.value.value().type()->GetFullName());

  // Test again, this time with $ prefix and Rust types.
  auto rust_context = MakeEvalContext(ExprLanguage::kRust);
  reg.called = false;
  GetNamedValue(rust_context, "$reg(x0)", &reg);

  EXPECT_FALSE(reg.called);

  loop().RunUntilNoTasks();
  EXPECT_TRUE(reg.called);
  EXPECT_FALSE(reg.value.has_error()) << reg.value.err().msg();

  EXPECT_EQ(kRegValue, reg.value.value().GetAs<uint64_t>());
  EXPECT_EQ("u64", reg.value.value().type()->GetFullName());

  // The value source should map back to the input register.
  const ExprValueSource& source = reg.value.value().source();
  EXPECT_EQ(ExprValueSource::Type::kRegister, source.type());
  EXPECT_EQ(debug::RegisterID::kARMv8_x0, source.register_id());
  EXPECT_FALSE(source.is_bitfield());

  // This register is synchronously known unavailable.
  provider()->AddRegisterValue(debug::RegisterID::kARMv8_x2, true, std::vector<uint8_t>{});
  reg.called = false;
  GetNamedValue(c_context, "x2", &reg);
  ASSERT_TRUE(reg.called);
  ASSERT_TRUE(reg.value.has_error());
  EXPECT_EQ("Register x2 unavailable in this context.", reg.value.err().msg());

  // This register is synchronously unavailable.
  provider()->AddRegisterValue(debug::RegisterID::kARMv8_x3, false, std::vector<uint8_t>{});
  reg.called = false;
  GetNamedValue(c_context, "x3", &reg);
  ASSERT_FALSE(reg.called);
  loop().RunUntilNoTasks();
  EXPECT_TRUE(reg.called);
  ASSERT_TRUE(reg.value.has_error());
  EXPECT_EQ("Register x3 unavailable in this context.", reg.value.err().msg());
}

TEST_F(EvalContextImplTest, RegisterShadowed) {
  constexpr uint64_t kRegValue = 0xdeadb33f;
  constexpr uint64_t kVarValue = 0xf00db4be;

  auto shadow_var = MakeUint64VariableForTest(
      "x0", kBeginValidRange, kEndValidRange,
      DwarfExpr({llvm::dwarf::DW_OP_reg1, llvm::dwarf::DW_OP_stack_value}));

  auto block = MakeCodeBlock();
  block->set_variables({LazySymbol(std::move(shadow_var))});

  provider()->set_ip(kBeginValidRange);
  provider()->AddRegisterValue(kDWARFReg0ID, false, kRegValue);
  provider()->AddRegisterValue(kDWARFReg1ID, false, kVarValue);
  auto context = MakeEvalContext(ExprLanguage::kC, block);

  // This should just look up our variable, x0, which is in the register x1. If It looks up the
  // register x0 something has gone very wrong.
  ValueResult val;
  GetNamedValue(context, "x0", &val);

  // Should not have been called yet since retrieving the register is asynchronous.
  EXPECT_FALSE(val.called);

  // Running the message loop should complete the callback.
  loop().RunUntilNoTasks();
  EXPECT_TRUE(val.called);
  ASSERT_FALSE(val.value.has_error()) << val.value.err().msg();
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(kVarValue)), val.value.value());

  // $ prefix should make the register show through.
  val.called = false;
  GetNamedValue(context, "$reg(x0)", &val);

  EXPECT_FALSE(val.called);

  loop().RunUntilNoTasks();
  EXPECT_TRUE(val.called);
  EXPECT_FALSE(val.value.has_error()) << val.value.err().msg();
  EXPECT_EQ(ExprValue(static_cast<uint64_t>(kRegValue)), val.value.value());
}

// Tests that a < 64-bit register is read into a value of the correct size, and that the
// pseudoregisters referring to a sub-part of a canonical register are working properly.
TEST_F(EvalContextImplTest, RegisterShort) {
  ASSERT_EQ(debug::Arch::kArm64, provider()->GetArch());

  // Value for the "w0" register. The mock data provider doesn't extract sub-registers (unlike the
  // real one) so we need to provide the exact enum the caller will request.
  constexpr uint64_t kRegValue = 0x44332211;
  provider()->AddRegisterValue(debug::RegisterID::kARMv8_w0, true, {0x11, 0x22, 0x33, 0x44});
  auto context = MakeEvalContext();

  // "w0" is the ARM64 way to refer to the low 32-bits of the "x0" register we set above.
  ValueResult reg;
  GetNamedValue(context, "w0", &reg);

  // Above we set the register to be returned synchronously.
  ASSERT_TRUE(reg.called);
  ASSERT_FALSE(reg.value.has_error()) << reg.value.err().msg();
  EXPECT_EQ(ExprValue(static_cast<uint32_t>(kRegValue)), reg.value.value());
  EXPECT_EQ("uint32_t", reg.value.value().type()->GetFullName());

  // Check source mapping.
  const ExprValueSource& source = reg.value.value().source();
  EXPECT_EQ(ExprValueSource::Type::kRegister, source.type());
  EXPECT_EQ(debug::RegisterID::kARMv8_w0, source.register_id());
  EXPECT_FALSE(source.is_bitfield());
}

// Extracts the "s" (low 32-bits) and "d" (low 64-bits) of the ARM vector registers.
TEST_F(EvalContextImplTest, FloatRegisterByName) {
  ASSERT_EQ(debug::Arch::kArm64, provider()->GetArch());

  // Value for the "d0" register. The mock data provider doesn't extract sub-registers (unlike the
  // real one) so we need to provide the exact enum the caller will request.
  constexpr double kDoubleValue = 3.14;
  std::vector<uint8_t> double_data(sizeof(double));
  memcpy(double_data.data(), &kDoubleValue, sizeof(double));
  provider()->AddRegisterValue(debug::RegisterID::kARMv8_d0, false, double_data);

  // Same for the "s1" register.
  constexpr float kFloatValue = 2.99;
  std::vector<uint8_t> float_data(sizeof(float));
  memcpy(float_data.data(), &kFloatValue, sizeof(float));
  provider()->AddRegisterValue(debug::RegisterID::kARMv8_s1, false, float_data);

  auto c_context = MakeEvalContext(ExprLanguage::kC);
  ValueResult reg;
  GetNamedValue(c_context, "d0", &reg);

  loop().RunUntilNoTasks();
  ASSERT_FALSE(reg.value.has_error()) << reg.value.err().msg();
  EXPECT_EQ("double", reg.value.value().type()->GetFullName());
  EXPECT_EQ(double_data, reg.value.value().data().bytes());

  GetNamedValue(c_context, "s1", &reg);

  loop().RunUntilNoTasks();
  ASSERT_FALSE(reg.value.has_error()) << reg.value.err().msg();
  EXPECT_EQ("float", reg.value.value().type()->GetFullName());
  EXPECT_EQ(float_data, reg.value.value().data().bytes());
}

TEST_F(EvalContextImplTest, VectorRegister) {
  // This just tests that vector formatting for vector registers is hooked up in the EvalContext
  // rather than trying to test all of the various formats. The EvalContextImpl formats all
  // vector registers as doubles (in real life the client overrides this to integrate with the
  // settings system).
  ASSERT_EQ(debug::Arch::kArm64, provider()->GetArch());

  // 128-bit vector register.
  std::vector<uint8_t> data{0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
                            0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};
  provider()->AddRegisterValue(debug::RegisterID::kARMv8_v0, true, data);
  auto context = MakeEvalContext();

  ValueResult reg;
  GetNamedValue(context, "v0", &reg);

  ASSERT_TRUE(reg.called);
  ASSERT_FALSE(reg.value.has_error()) << reg.value.err().msg();

  EXPECT_EQ("double[2]", reg.value.value().type()->GetFullName());

  // The data should be passed through unchanged, the array code will handle unpacking it.
  EXPECT_EQ(data, reg.value.value().data().bytes());

  // Check source mapping.
  const ExprValueSource& source = reg.value.value().source();
  EXPECT_EQ(ExprValueSource::Type::kRegister, source.type());
  EXPECT_EQ(debug::RegisterID::kARMv8_v0, source.register_id());
  EXPECT_FALSE(source.is_bitfield());
}

TEST_F(EvalContextImplTest, DataResult) {
  // Tests that composite variable locations are properly converted to values.
  const char kVarName[] = "var";
  auto variable = MakeUint64VariableForTest(
      kVarName,
      VariableLocation(DwarfExpr({llvm::dwarf::DW_OP_reg0,             // Low bytes in reg0.
                                  llvm::dwarf::DW_OP_piece, 0x04,      // Pick low 4 bytes of reg0.
                                  llvm::dwarf::DW_OP_reg1,             // High bytes in reg1.
                                  llvm::dwarf::DW_OP_piece, 0x04})));  // Pick low 4 of reg1.
  provider()->AddRegisterValue(debug::RegisterID::kARMv8_x0, true, 1);
  provider()->AddRegisterValue(debug::RegisterID::kARMv8_x1, true, 2);

  auto block = MakeCodeBlock();
  block->set_variables({LazySymbol(variable)});
  auto context = MakeEvalContext(ExprLanguage::kC, block);

  ValueResult val;
  GetNamedValue(context, kVarName, &val);
  EXPECT_TRUE(val.called);  // Result should be synchronous.

  ASSERT_FALSE(val.value.has_error()) << val.value.err().msg();
  std::vector<uint8_t> expected{1, 0, 0, 0, 2, 0, 0, 0};
  EXPECT_EQ(expected, val.value.value().data().bytes());
  EXPECT_EQ(ExprValueSource::Type::kComposite, val.value.value().source().type());
}

}  // namespace zxdb
