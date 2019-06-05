// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/find_name.h"

#include "gtest/gtest.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "src/developer/debug/zxdb/expr/eval_test_support.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/identifier.h"
#include "src/developer/debug/zxdb/symbols/index_test_support.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/symbol_context.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"
#include "src/lib/fxl/logging.h"

// NOTE: Finding variables on *this* and subclasses is
// EvalContextImplTest.FoundThis which tests both of our file's finding code
// as well as the decoding code.

namespace zxdb {

// This test declares the following structure. There are three levels of
// variables, each one has one unique variable, and one labeled "value" for
// testing ambiguity.
//
// namespace ns {
//
// int32_t ns_value;
//
// void Foo(int32_t value, int32_t other_param) {
//   int32_t value;  // 2nd declaration.
//   int32_t function_local;
//   {
//     int32_t value;  // 3rd declaration.
//     int32_t block_local;
//   }
// }
//
// }  // namespace ns
TEST(FindName, FindLocalVariable) {
  ProcessSymbolsTestSetup setup;

  auto int32_type = MakeInt32Type();

  // Empty DWARF location expression. Since we don't evaluate any variables
  // they can all be empty.
  std::vector<uint8_t> var_loc;

  // Set up the module symbols. This creates "ns" and "ns_value" in the
  // symbol index.
  auto mod = std::make_unique<MockModuleSymbols>("mod.so");
  auto& root = mod->index().root();  // Root of the index for module 1.

  const char kNsName[] = "ns";
  auto ns_node = root.AddChild(kNsName);

  const char kNsVarName[] = "ns_value";
  TestIndexedGlobalVariable ns_value(mod.get(), ns_node, kNsVarName);

  constexpr uint64_t kLoadAddress = 0x1000;
  SymbolContext symbol_context(kLoadAddress);
  setup.InjectModule("mod", "1234", kLoadAddress, std::move(mod));

  // Namespace.
  auto ns = fxl::MakeRefCounted<Namespace>();
  ns->set_assigned_name(kNsName);

  // Function inside the namespace.
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("function");
  uint64_t kFunctionBeginAddr = 0x1000;
  uint64_t kFunctionEndAddr = 0x2000;
  function->set_code_ranges(
      AddressRanges(AddressRange(kFunctionBeginAddr, kFunctionEndAddr)));
  function->set_parent(LazySymbol(ns));

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
  FindNameContext function_context(&setup.process(), symbol_context,
                                   function.get());

  // Inner block.
  uint64_t kBlockBeginAddr = 0x1100;
  uint64_t kBlockEndAddr = 0x1200;
  auto block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  block->set_code_ranges(
      AddressRanges(AddressRange(kBlockBeginAddr, kBlockEndAddr)));
  block->set_parent(LazySymbol(function));
  function->set_inner_blocks({LazySymbol(block)});

  // Inner block variables.
  auto block_value = MakeVariableForTest("value", int32_type, kBlockBeginAddr,
                                         kBlockEndAddr, var_loc);
  auto block_other = MakeVariableForTest(
      "block_local", int32_type, kBlockBeginAddr, kBlockEndAddr, var_loc);
  block->set_variables({LazySymbol(block_value), LazySymbol(block_other)});
  FindNameContext block_context(&setup.process(), symbol_context, block.get());

  // ACTUAL TEST CODE ----------------------------------------------------------

  FindNameOptions all_kinds(FindNameOptions::kAllKinds);

  // Find "value" in the nested block should give the block's one.
  ParsedIdentifier value_ident(var_value->GetAssignedName());
  FoundName found = FindName(block_context, all_kinds, value_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(block_value.get(), found.variable());

  // Find "value" in the function block should give the function's one.
  found = FindName(function_context, all_kinds, value_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(var_value.get(), found.variable());
  EXPECT_EQ(var_value->GetAssignedName(), found.GetName());

  // Find "::value" should match nothing.
  ParsedIdentifier value_global_ident(
      IdentifierQualification::kGlobal,
      ParsedIdentifierComponent(var_value->GetAssignedName()));
  found = FindName(function_context, all_kinds, value_global_ident);
  EXPECT_FALSE(found);

  // Prefix search for "va" should find all three "values".
  std::vector<FoundName> found_vector;
  FindNameOptions prefix_options(FindNameOptions::kAllKinds);
  prefix_options.how = FindNameOptions::kPrefix;
  prefix_options.max_results = 100;
  ParsedIdentifier va_identifier("va");
  FindLocalVariable(prefix_options, block.get(), va_identifier, &found_vector);
  ASSERT_EQ(3u, found_vector.size());

  // Limiting the prefix result set to 1 should only fine one.
  prefix_options.max_results = 1;
  found_vector.clear();
  FindLocalVariable(prefix_options, block.get(), va_identifier, &found_vector);
  ASSERT_EQ(1u, found_vector.size());

  // Find "block_local" in the block should be found, but in the function it
  // should not be.
  ParsedIdentifier block_local_ident(block_other->GetAssignedName());
  found = FindName(block_context, all_kinds, block_local_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(block_other.get(), found.variable());
  EXPECT_EQ(block_other->GetAssignedName(), found.GetName());
  found = FindName(function_context, all_kinds, block_local_ident);
  EXPECT_FALSE(found);

  // Finding the other function parameter in the block should work.
  ParsedIdentifier other_param_ident(param_other->GetAssignedName());
  found = FindName(block_context, all_kinds, other_param_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(param_other.get(), found.variable());

  // Look up the variable "ns::ns_value" using the name "ns_value" (no
  // namespace) from within the context of the "ns::function()" function.
  // The namespace of the function should be implicitly picked up.
  ParsedIdentifier ns_value_ident(kNsVarName);
  found = FindName(block_context, all_kinds, ns_value_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(ns_value.var.get(), found.variable());
  EXPECT_EQ(kNsVarName, found.GetName());

  // Loop up the global "ns_value" var with no global symbol context. This
  // should fail and not crash.
  FindNameContext block_no_modules_context;
  block_no_modules_context.block = block.get();
  found = FindName(block_no_modules_context, all_kinds, ns_value_ident);
  EXPECT_FALSE(found);

  // Break reference cycle for test teardown.
  function->set_parent(LazySymbol());
  block->set_parent(LazySymbol());
}

// This test only tests for finding object members. It doesn't set up the
// index which might find types, that's tested by FindIndexedName.
TEST(FindName, FindMember) {
  ProcessSymbolsTestSetup setup;
  DerivedClassTestSetup d;

  FindNameContext context;  // Empty context = local and object vars only.
  FindNameOptions exact_var(FindNameOptions::kAllKinds);

  // The two base classes each have a "b" member.
  ParsedIdentifier b_ident("b");

  // Finding one member "b" should find the first one (Base1) because the
  // options find the first match by default.
  std::vector<FoundName> results;
  FindMember(context, exact_var, d.derived_type.get(), b_ident, nullptr,
             &results);
  ASSERT_EQ(1u, results.size());
  ASSERT_EQ(FoundName::kMemberVariable, results[0].kind());
  EXPECT_EQ(d.kBase1Offset, results[0].member().data_member_offset());
  EXPECT_EQ("b", results[0].GetName());

  // Increase the limit, it should find both in order of Base1, Base2.
  results.clear();
  exact_var.max_results = 100;
  FindMember(context, exact_var, d.derived_type.get(), b_ident, nullptr,
             &results);
  ASSERT_EQ(2u, results.size());
  ASSERT_EQ(FoundName::kMemberVariable, results[0].kind());
  ASSERT_EQ(FoundName::kMemberVariable, results[1].kind());
  EXPECT_EQ(d.kBase1Offset, results[0].member().data_member_offset());
  EXPECT_EQ(d.kBase2Offset, results[1].member().data_member_offset());
}

// This only tests the ModuleSymbols and function naming integration, the
// details of the index searching are tested by FindGlobalNameInModule()
TEST(FindName, FindIndexedName) {
  ProcessSymbolsTestSetup setup;

  const char kGlobalName[] = "global";  // Different variable in each.
  const char kVar1Name[] = "var1";      // Only in module 1
  const char kVar2Name[] = "var2";      // Only in module 2
  const char kNotFoundName[] = "notfound";

  ParsedIdentifier global_ident(kGlobalName);
  ParsedIdentifier var1_ident(kVar1Name);
  ParsedIdentifier var2_ident(kVar2Name);
  ParsedIdentifier notfound_ident(kNotFoundName);

  // Module 1.
  auto mod1 = std::make_unique<MockModuleSymbols>("mod1.so");
  auto& root1 = mod1->index().root();  // Root of the index for module 1.
  TestIndexedGlobalVariable global1(mod1.get(), &root1, kGlobalName);
  TestIndexedGlobalVariable var1(mod1.get(), &root1, kVar1Name);
  constexpr uint64_t kLoadAddress1 = 0x1000;
  SymbolContext symbol_context1(kLoadAddress1);
  setup.InjectModule("mod1", "1234", kLoadAddress1, std::move(mod1));

  // Module 2.
  auto mod2 = std::make_unique<MockModuleSymbols>("mod2.so");
  auto& root2 = mod2->index().root();  // Root of the index for module 1.
  TestIndexedGlobalVariable global2(mod2.get(), &root2, kGlobalName);
  TestIndexedGlobalVariable var2(mod2.get(), &root2, kVar2Name);
  constexpr uint64_t kLoadAddress2 = 0x2000;
  SymbolContext symbol_context2(kLoadAddress2);
  setup.InjectModule("mod2", "5678", kLoadAddress2, std::move(mod2));

  FindNameOptions all_opts(FindNameOptions::kAllKinds);
  std::vector<FoundName> found;

  // Searching for "global" in module1's context should give the global in that
  // module.
  FindNameContext mod1_context(&setup.process(), symbol_context1);
  FindIndexedName(mod1_context, all_opts, ParsedIdentifier(), global_ident,
                  true, &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global1.var.get(), found[0].variable());

  // Searching for "global" in module2's context should give the global in that
  // module.
  found.clear();
  FindNameContext mod2_context(&setup.process(), symbol_context2);
  FindIndexedName(mod2_context, all_opts, ParsedIdentifier(), global_ident,
                  true, &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global2.var.get(), found[0].variable());

  // Searching for "var1" in module2's context should still find it even though
  // its in the other module.
  found.clear();
  FindIndexedName(mod2_context, all_opts, ParsedIdentifier(), var1_ident, true,
                  &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(var1.var.get(), found[0].variable());

  // Searching for "var2" with only target-level symbols should still find it.
  found.clear();
  FindIndexedName(FindNameContext(&setup.target()), all_opts,
                  ParsedIdentifier(), var2_ident, true, &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(var2.var.get(), found[0].variable());
}

TEST(FindName, FindIndexedNameInModule) {
  MockModuleSymbols mod_sym("test.so");

  auto& root = mod_sym.index().root();  // Root of the index.

  const char kVarName[] = "var";
  const char kNsName[] = "ns";

  FindNameOptions all_opts(FindNameOptions::kAllKinds);
  std::vector<FoundName> found;

  // Make a global variable in the toplevel namespace.
  TestIndexedGlobalVariable global(&mod_sym, &root, kVarName);

  ParsedIdentifier var_ident(kVarName);
  FindIndexedNameInModule(all_opts, &mod_sym, ParsedIdentifier(), var_ident,
                          true, &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global.var.get(), found[0].variable());

  // Say we're in some nested namespace and search for the same name. It should
  // find the variable in the upper namespace.
  ParsedIdentifier nested_ns(kNsName);
  found.clear();
  FindIndexedNameInModule(all_opts, &mod_sym, nested_ns, var_ident, true,
                          &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global.var.get(), found[0].variable());

  // Add a variable in the nested namespace with the same name.
  auto ns_node = root.AddChild(kNsName);
  TestIndexedGlobalVariable ns(&mod_sym, ns_node, kVarName);

  // Re-search for the same name in the nested namespace, it should get the
  // nested one first.
  found.clear();
  FindIndexedNameInModule(all_opts, &mod_sym, nested_ns, var_ident, true,
                          &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(ns.var.get(), found[0].variable());

  // Now do the same search but globally qualify the input "::var" which should
  // match only the toplevel one.
  ParsedIdentifier var_global_ident(IdentifierQualification::kGlobal,
                                    ParsedIdentifierComponent(kVarName));
  found.clear();
  FindIndexedNameInModule(all_opts, &mod_sym, nested_ns, var_global_ident, true,
                          &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global.var.get(), found[0].variable());
  EXPECT_EQ(kVarName, found[0].GetName());
}

TEST(FindName, FindTypeName) {
  ProcessSymbolsTestSetup setup;
  auto mod = std::make_unique<MockModuleSymbols>("mod.so");
  auto& root = mod->index().root();  // Root of the index for module 1.

  // Note space in "> >" which is how Clang likes to represent this.
  const char kGlobalTypeName[] = "GlobalType<std::char_traits<char> >";
  const char kChildTypeName[] = "ChildType<std::char_traits<char> >";

  // Global class name.
  ParsedIdentifier global_type_name(kGlobalTypeName);
  auto global_type = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  global_type->set_assigned_name(kGlobalTypeName);
  TestIndexedSymbol global_indexed(mod.get(), &root, kGlobalTypeName,
                                   global_type);

  // Child type definition inside the global class name. Currently types don't
  // have child types and everything is found via the index.
  ParsedIdentifier child_type_name(kChildTypeName);
  ParsedIdentifier full_child_type_name;
  Err err = ExprParser::ParseIdentifier(
      "GlobalType<std::char_traits<char> >::ChildType<std::char_traits<char> >",
      &full_child_type_name);
  ASSERT_FALSE(err.has_error());
  auto child_type = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  child_type->set_assigned_name(kChildTypeName);
  TestIndexedSymbol child_indexed(mod.get(), global_indexed.index_node,
                                  kChildTypeName, child_type);

  // Declares a variable that points to the GlobalType. It will be the "this"
  // pointer for the function. The address range of this variable doesn't
  // overlap the function. This means we can never compute its value, but since
  // it's syntactically in-scope, we should still be able to use its type
  // to resolve type names on the current class.
  auto global_type_ptr = fxl::MakeRefCounted<ModifiedType>(
      DwarfTag::kPointerType, LazySymbol(global_type));
  auto this_var = MakeVariableForTest(
      "this", global_type_ptr, 0x9000, 0x9001,
      {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});

  // Function as a member of GlobalType.
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("function");
  uint64_t kFunctionBeginAddr = 0x1000;
  uint64_t kFunctionEndAddr = 0x2000;
  function->set_code_ranges(
      AddressRanges(AddressRange(kFunctionBeginAddr, kFunctionEndAddr)));
  function->set_object_pointer(LazySymbol(this_var));

  // This context declares a target and a block but no current module, which
  // means the block and all modules should be searched with no particular
  // preference (most other code sets a preference so this tests that less
  // common case).
  FindNameContext function_context;
  function_context.target_symbols = &setup.target();
  function_context.block = function.get();

  // Warning: this moves out the "mod" variable so all variable setup needs to
  // go before here.
  constexpr uint64_t kLoadAddress = 0x1000;
  SymbolContext symbol_context(kLoadAddress);
  setup.InjectModule("mod", "1234", kLoadAddress, std::move(mod));

  // ACTUAL TEST CODE ----------------------------------------------------------

  FindNameOptions all_kinds(FindNameOptions::kAllKinds);

  // Look up from the global function.
  FoundName found = FindName(function_context, all_kinds, global_type_name);
  EXPECT_TRUE(found);
  EXPECT_EQ(FoundName::kType, found.kind());
  EXPECT_EQ(global_type.get(), found.type().get());
  EXPECT_EQ(kGlobalTypeName, found.GetName());

  // Prefix search same as above.
  FindNameOptions prefix_opts(FindNameOptions::kAllKinds);
  prefix_opts.how = FindNameOptions::kPrefix;
  prefix_opts.max_results = 10000;
  std::vector<FoundName> found_vect;
  ParsedIdentifier global_type_prefix("Gl");
  FindName(function_context, prefix_opts, global_type_prefix, &found_vect);
  ASSERT_EQ(1u, found_vect.size());
  EXPECT_EQ(global_type.get(), found_vect[0].type().get());

  // Look up the child function by full name.
  found = FindName(function_context, all_kinds, full_child_type_name);
  EXPECT_TRUE(found);
  EXPECT_EQ(FoundName::kType, found.kind());
  EXPECT_EQ(child_type.get(), found.type().get());

  // Look up the child function by just the child name. Since the function is
  // a member of GlobalType, ChildType is a member of "this" so it should be
  // found.
  found = FindName(function_context, all_kinds, child_type_name);
  EXPECT_TRUE(found);
  EXPECT_EQ(FoundName::kType, found.kind());
  EXPECT_EQ(child_type.get(), found.type().get());
}

TEST(FindName, FindTemplateName) {
  ProcessSymbolsTestSetup setup;
  auto mod = std::make_unique<MockModuleSymbols>("mod.so");
  auto& root = mod->index().root();  // Root of the index for module 1.

  // Declare two functions, one's a template, the other has the same prefix but
  // isn't.
  const char kTemplateIntName[] = "Template<int>";
  const char kTemplateNotName[] = "TemplateNot";

  ParsedIdentifier template_int_name(kTemplateIntName);
  ParsedIdentifier template_not_name(kTemplateNotName);

  auto template_int = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  template_int->set_assigned_name(kTemplateIntName);
  TestIndexedSymbol template_int_indexed(mod.get(), &root, kTemplateIntName,
                                         template_int);

  auto template_not = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  template_not->set_assigned_name(kTemplateNotName);
  TestIndexedSymbol template_not_indexed(mod.get(), &root, kTemplateNotName,
                                         template_not);

  // Search for names globally within the target.
  FindNameContext context(&setup.target());

  // Warning: this moves out the "mod" variable so all variable setup needs to
  // go before here.
  constexpr uint64_t kLoadAddress = 0x1000;
  SymbolContext symbol_context(kLoadAddress);
  setup.InjectModule("mod", "1234", kLoadAddress, std::move(mod));

  FindNameOptions all_types(FindNameOptions::kAllKinds);

  // The string "Template" should be identified as one.
  ParsedIdentifier template_name("Template");
  auto found = FindName(context, all_types, template_name);
  EXPECT_TRUE(found);
  EXPECT_EQ(FoundName::kTemplate, found.kind());
  EXPECT_EQ("Template", found.GetName());

  // The string "TemplateNot" is a type, it should be found as such.
  std::vector<FoundName> found_vect;
  FindName(context, all_types, template_not_name, &found_vect);
  ASSERT_EQ(1u, found_vect.size());
  EXPECT_EQ(FoundName::kType, found_vect[0].kind());

  // Now search only for templates, "TemplateNot" should not be found.
  found_vect.clear();
  FindNameOptions templates_only(FindNameOptions::kNoKinds);
  templates_only.find_templates = true;
  FindName(context, templates_only, template_not_name, &found_vect);
  EXPECT_TRUE(found_vect.empty());

  // Prefix search for "Templ" should get both full types. Since prefix
  // searching doesn't currently work for templates, we won't get a template
  // record. These results will need to be updated if template prefix matching
  // is added.
  found_vect.clear();
  FindNameOptions all_prefixes(FindNameOptions::kAllKinds);
  all_prefixes.how = FindNameOptions::kPrefix;
  all_prefixes.max_results = 100;
  ParsedIdentifier templ_name("Templ");
  FindName(context, all_prefixes, templ_name, &found_vect);
  ASSERT_EQ(2u, found_vect.size());
  // Both results are types.
  EXPECT_EQ(FoundName::kType, found_vect[0].kind());
  EXPECT_EQ(FoundName::kType, found_vect[1].kind());
  // Can appear in either order.
  EXPECT_TRUE((found_vect[0].type().get() == template_int.get() &&
               found_vect[1].type().get() == template_not.get()) ||
              (found_vect[0].type().get() == template_not.get() &&
               found_vect[1].type().get() == template_int.get()));
}

}  // namespace zxdb
