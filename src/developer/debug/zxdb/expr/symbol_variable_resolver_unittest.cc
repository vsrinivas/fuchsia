// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/symbol_variable_resolver.h"
#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/shared/platform_message_loop.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/mock_symbol_data_provider.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"

namespace zxdb {

namespace {

class SymbolVariableResolverTest : public testing::Test {
 public:
  SymbolVariableResolverTest()
      : provider_(fxl::MakeRefCounted<MockSymbolDataProvider>()) {
    loop_.Init();
  }
  ~SymbolVariableResolverTest() { loop_.Cleanup(); }

  fxl::RefPtr<MockSymbolDataProvider> provider() { return provider_; }
  debug_ipc::MessageLoop& loop() { return loop_; }

 private:
  debug_ipc::PlatformMessageLoop loop_;
  fxl::RefPtr<MockSymbolDataProvider> provider_;
};

const debug_ipc::RegisterID kDWARFReg0ID = debug_ipc::RegisterID::kARMv8_x0;

}  // namespace

// Test a lookup of the value where everything is found.
TEST_F(SymbolVariableResolverTest, FoundAndNot) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);

  provider()->set_ip(0x1010);

  // Declare a variable in register 0.
  auto var = MakeUint64VariableForTest(
      "present", 0x1000, 0x2000,
      {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();
  SymbolVariableResolver resolver(provider());

  bool called = false;
  Err out_err;
  ExprValue out_value;
  resolver.ResolveVariable(
      symbol_context, var.get(),
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
      });

  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(ExprValue(kValue), out_value);
}

// This lookup has the IP out of range of the variable's validity.
TEST_F(SymbolVariableResolverTest, RangeMiss) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);

  provider()->set_ip(0x3000);
  auto var = MakeUint64VariableForTest(
      "present", 0x1000, 0x2000,
      {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();
  SymbolVariableResolver resolver(provider());

  bool called = false;
  Err out_err;
  ExprValue out_value;
  resolver.ResolveVariable(
      symbol_context, var.get(),
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
      });

  EXPECT_TRUE(called);
  EXPECT_TRUE(out_err.has_error());
  EXPECT_EQ(ErrType::kOptimizedOut, out_err.type());
  EXPECT_EQ(ExprValue(), out_value);
}

// Tests the DWARF expression evaluation failing (empty expression).
TEST_F(SymbolVariableResolverTest, DwarfEvalFailure) {
  auto var = MakeUint64VariableForTest("present", 0x1000, 0x2000, {});

  provider()->set_ip(0x1000);
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();
  SymbolVariableResolver resolver(provider());

  bool called = false;
  Err out_err;
  ExprValue out_value;
  resolver.ResolveVariable(
      symbol_context, var.get(),
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
      });

  EXPECT_TRUE(called);
  EXPECT_TRUE(out_err.has_error());
  EXPECT_EQ("DWARF expression produced no results.", out_err.msg());
  EXPECT_EQ(ExprValue(), out_value);
}

TEST_F(SymbolVariableResolverTest, CharValue) {
  constexpr uint64_t kValue = 0x1234567890123;
  constexpr int8_t kValueLo = 0x23;  // Low byte of kValue.
  provider()->AddRegisterValue(kDWARFReg0ID, true, kValue);
  provider()->set_ip(0x1010);

  // Make a variable and override type with a "char" type.
  auto var = MakeUint64VariableForTest(
      "present", 0x1000, 0x2000,
      {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});
  auto type =
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeSigned, 1, "char");
  var->set_type(LazySymbol(type));

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();
  SymbolVariableResolver resolver(provider());

  bool called = false;
  Err out_err;
  ExprValue out_value;
  resolver.ResolveVariable(
      symbol_context, var.get(),
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
      });

  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();

  // Should have inherited our type.
  EXPECT_EQ(type.get(), out_value.type());
  ASSERT_EQ(1u, out_value.data().size());
  EXPECT_EQ(kValueLo, static_cast<int8_t>(out_value.data()[0]));

  EXPECT_EQ(ExprValue(kValueLo), out_value);
}

// Tests asynchronously reading an integer from memory. This also tests
// interleaved execution of multiple requests by having a resolution miss
// request execute while the memory request is pending.
TEST_F(SymbolVariableResolverTest, IntOnStack) {
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

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();
  SymbolVariableResolver resolver(provider());

  bool called = false;
  Err out_err;
  ExprValue out_value;
  resolver.ResolveVariable(
      symbol_context, var.get(),
      [&called, &out_err, &out_value](const Err& err, ExprValue value) {
        called = true;
        out_err = err;
        out_value = value;
        debug_ipc::MessageLoop::Current()->QuitNow();
      });

  // Should be run async since it requests memory.
  EXPECT_FALSE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();

  // Before running the loop and receiving the memory, start a new request,
  // this one will fail synchronously due to a range miss.
  auto rangemiss = MakeUint64VariableForTest("rangemiss", 0x6000, 0x7000,
                                             {llvm::dwarf::DW_OP_reg0});
  bool called2 = false;
  Err out_err2;
  ExprValue out_value2;
  resolver.ResolveVariable(
      symbol_context, rangemiss.get(),
      [&called2, &out_err2, &out_value2](const Err& err, ExprValue value) {
        called2 = true;
        out_err2 = err;
        out_value2 = value;
      });
  EXPECT_TRUE(called2);
  EXPECT_TRUE(out_err2.has_error());
  EXPECT_EQ(ErrType::kOptimizedOut, out_err2.type());
  EXPECT_EQ(ExprValue(), out_value2);

  // Now let the first request complete.
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error()) << out_err.msg();
  EXPECT_EQ(ExprValue(kValue), out_value);
}

}  // namespace zxdb
