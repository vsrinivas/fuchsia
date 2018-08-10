// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/symbol_variable_resolver.h"
#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/mock_symbol_data_provider.h"
#include "garnet/bin/zxdb/client/symbols/symbol_context.h"
#include "garnet/bin/zxdb/client/symbols/variable_test_support.h"
#include "garnet/bin/zxdb/expr/expr_value.h"
#include "garnet/lib/debug_ipc/helper/platform_message_loop.h"
#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"

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

}  // namespace

// Test a lookup of the value where everything is found.
TEST_F(SymbolVariableResolverTest, Found) {
  constexpr uint64_t kValue = 0x1234567890123;
  provider()->AddRegisterValue(0, true, kValue);

  provider()->set_ip(0x1010);
  auto var = MakeUint64VariableForTest("present", 0x1000, 0x2000,
                                       {llvm::dwarf::DW_OP_reg0});

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
  provider()->AddRegisterValue(0, true, kValue);

  provider()->set_ip(0x3000);
  auto var = MakeUint64VariableForTest("present", 0x1000, 0x2000,
                                       {llvm::dwarf::DW_OP_reg0});

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
  provider()->AddRegisterValue(0, true, kValue);
  provider()->set_ip(0x1010);

  // Make a variable and override type with a "char" type.
  auto var = MakeUint64VariableForTest("present", 0x1000, 0x2000,
                                       {llvm::dwarf::DW_OP_reg0});
  auto type = fxl::MakeRefCounted<BaseType>();
  type->set_byte_size(1);
  type->set_base_type(BaseType::kBaseTypeSigned);
  type->set_assigned_name("char");
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

}  // namespace zxdb
