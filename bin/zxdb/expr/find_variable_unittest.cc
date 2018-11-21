// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/find_variable.h"
#include "garnet/bin/zxdb/expr/found_variable.h"
#include "garnet/bin/zxdb/expr/identifier.h"
#include "garnet/bin/zxdb/symbols/base_type.h"
#include "garnet/bin/zxdb/symbols/function.h"
#include "garnet/bin/zxdb/symbols/mock_module_symbols.h"
#include "garnet/bin/zxdb/symbols/symbol_context.h"
#include "garnet/bin/zxdb/symbols/type_test_support.h"
#include "garnet/bin/zxdb/symbols/variable_test_support.h"
#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"

// NOTE: Finding variables on *this* and subclasses is
// SymbolEvalContextTest.FoundThis which tests both of our file's finding code
// as well as the decoding code.

namespace zxdb {

// This test declares the following structure. There are three levels of
// variables, each one has one unique variable, and one labeled "value" for
// testing ambiguity.
//
// void Foo(int32_t value, int32_t other_param) {
//   int32_t value;  // 2nd declaration.
//   int32_t function_local;
//   {
//     int32_t value;  // 3rd declaration.
//     int32_t block_local;
//   }
// }
TEST(FindVariable, FindLocalVariable) {
  auto int32_type = MakeInt32Type();

  // Empyt DWARF location expression. Since we don't evaluate any variables
  // they can all be empty.
  std::vector<uint8_t> var_loc;

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  // Function.
  auto function = fxl::MakeRefCounted<Function>();
  function->set_assigned_name("function");
  uint64_t kFunctionBeginAddr = 0x1000;
  uint64_t kFunctionEndAddr = 0x2000;
  function->set_code_ranges(
      {AddressRange(kFunctionBeginAddr, kFunctionEndAddr)});

  // Function parameters.
  auto param_value = MakeVariableForTest(
      "value", int32_type, kFunctionBeginAddr, kFunctionEndAddr, var_loc);
  auto param_other = MakeVariableForTest(
      "other_param", int32_type, kFunctionBeginAddr, kFunctionEndAddr, var_loc);
  function->set_parameters({LazySymbol(param_value), LazySymbol(param_other)});

  // Function local variables.
  auto var_value = MakeVariableForTest("value", int32_type, kFunctionBeginAddr,
                                       kFunctionEndAddr, var_loc);
  auto var_other =
      MakeVariableForTest("function_local", int32_type, kFunctionBeginAddr,
                          kFunctionEndAddr, var_loc);
  function->set_variables({LazySymbol(var_value), LazySymbol(var_other)});

  // Inner block.
  uint64_t kBlockBeginAddr = 0x1100;
  uint64_t kBlockEndAddr = 0x1200;
  auto block = fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);
  block->set_code_ranges({AddressRange(kBlockBeginAddr, kBlockEndAddr)});
  block->set_parent(LazySymbol(function));
  function->set_inner_blocks({LazySymbol(block)});

  // Inner block variables.
  auto block_value = MakeVariableForTest("value", int32_type, kBlockBeginAddr,
                                         kBlockEndAddr, var_loc);
  auto block_other = MakeVariableForTest(
      "block_local", int32_type, kBlockBeginAddr, kBlockEndAddr, var_loc);
  block->set_variables({LazySymbol(block_value), LazySymbol(block_other)});

  // Find "value" in the nested block should give the block's one.
  Identifier value_ident(
      ExprToken(ExprToken::kName, var_value->GetAssignedName(), 0));
  auto found = FindVariable(nullptr, block.get(), &symbol_context, value_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(block_value.get(), found->variable());

  // Find "value" in the function block should give the function's one.
  found = FindVariable(nullptr, function.get(), &symbol_context, value_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(var_value.get(), found->variable());

  // Find "::value" should match nothing.
  Identifier value_global_ident(Identifier::Component(
      ExprToken(ExprToken::kColonColon, "::", 0),
      ExprToken(ExprToken::kName, var_value->GetAssignedName(), 0)));
  found = FindVariable(nullptr, function.get(), &symbol_context,
                       value_global_ident);
  EXPECT_FALSE(found);

  // Find "block_local" in the block should be found, but in the function it
  // should not be.
  Identifier block_local_ident(
      ExprToken(ExprToken::kName, block_other->GetAssignedName(), 0));
  found =
      FindVariable(nullptr, block.get(), &symbol_context, block_local_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(block_other.get(), found->variable());
  found =
      FindVariable(nullptr, function.get(), &symbol_context, block_local_ident);
  EXPECT_FALSE(found);

  // Finding the other function parameter in the block should work.
  Identifier other_param_ident(
      ExprToken(ExprToken::kName, param_other->GetAssignedName(), 0));
  found =
      FindVariable(nullptr, block.get(), &symbol_context, other_param_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(param_other.get(), found->variable());

  // Break reference cycle for test teardown.
  block->set_parent(LazySymbol());
}

TEST(FindVariable, FindGlobalVariableInModule) {
  MockModuleSymbols mod_sym("test.so");

  auto int32_type = MakeInt32Type();
  auto& root = mod_sym.index().root();  // Root of the index.

  const char kVarName[] = "var";
  const char kNsName[] = "ns";

  // Empyt DWARF location expression. Since we don't evaluate any variables
  // they can all be empty.
  std::vector<uint8_t> var_loc;

  // Make a global variable in the toplevel namespace.
  ModuleSymbolIndexNode::DieRef global_ref(1);
  auto global_node = root.AddChild(kVarName);
  global_node->AddDie(global_ref);
  auto global_var =
      MakeVariableForTest(kVarName, int32_type, 0x100, 0x200, var_loc);
  mod_sym.AddDieRef(global_ref, global_var);

  Identifier var_ident(Identifier::Component(
      ExprToken(), ExprToken(ExprToken::kName, kVarName, 0)));
  auto found = FindGlobalVariableInModule(&mod_sym, Identifier(), var_ident);
  ASSERT_TRUE(found);
  EXPECT_EQ(global_var.get(), found->variable());

  // Say we're in some nested namespace and search for the same name. It should
  // find the variable in the upper namespace.
  Identifier nested_ns(Identifier::Component(
      ExprToken(), ExprToken(ExprToken::kName, kNsName, 0)));
  found = FindGlobalVariableInModule(&mod_sym, nested_ns, var_ident);
  ASSERT_TRUE(found);
  EXPECT_EQ(global_var.get(), found->variable());

  // Add a variable in the nested namespace with the same name.
  ModuleSymbolIndexNode::DieRef ns_var_ref(2);
  auto ns_node = root.AddChild(kNsName);
  auto ns_var_node = ns_node->AddChild(kVarName);
  ns_var_node->AddDie(ns_var_ref);
  auto ns_var =
      MakeVariableForTest(kVarName, int32_type, 0x300, 0x400, var_loc);
  mod_sym.AddDieRef(ns_var_ref, ns_var);

  // Re-search for the same name in the nested namespace, it should get the
  // nested one first.
  found = FindGlobalVariableInModule(&mod_sym, nested_ns, var_ident);
  ASSERT_TRUE(found);
  EXPECT_EQ(ns_var.get(), found->variable());

  // Now do the same search but globally qualify the input "::var" which should
  // match only the toplevel one.
  Identifier var_global_ident(
      Identifier::Component(ExprToken(ExprToken::kColonColon, "::", 0),
                            ExprToken(ExprToken::kName, kVarName, 0)));
  found = FindGlobalVariableInModule(&mod_sym, nested_ns, var_global_ident);
  ASSERT_TRUE(found);
  EXPECT_EQ(global_var.get(), found->variable());
}

}  // namespace zxdb
