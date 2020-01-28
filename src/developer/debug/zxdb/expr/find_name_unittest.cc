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
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"
#include "src/lib/fxl/logging.h"

// NOTE: Finding variables on *this* and subclasses is EvalContextImplTest.FoundThis which tests
// both of our file's finding code as well as the decoding code.

namespace zxdb {

// This test declares the following structure. There are three levels of variables, each one has one
// unique variable, and one labeled "value" for testing ambiguity.
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
  MockModuleSymbols* module_symbols = setup.InjectMockModule();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);
  auto& index_root = module_symbols->index().root();

  auto int32_type = MakeInt32Type();

  // Empty DWARF location expression. Since we don't evaluate any variables they can all be empty.
  std::vector<uint8_t> var_loc;

  // Set up the module symbols. This creates "ns" and "ns_value" in the symbol index.
  const char kNsName[] = "ns";
  auto ns_node = index_root.AddChild(IndexNode::Kind::kNamespace, kNsName, IndexNode::DieRef());
  const char kNsVarName[] = "ns_value";
  TestIndexedGlobalVariable ns_value(module_symbols, ns_node, kNsVarName);

  // Namespace.
  auto ns = fxl::MakeRefCounted<Namespace>();
  ns->set_assigned_name(kNsName);

  // Function inside the namespace.
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("function");
  uint64_t kFunctionBeginAddr = ProcessSymbolsTestSetup::kDefaultLoadAddress + 0x1000;
  uint64_t kFunctionEndAddr = ProcessSymbolsTestSetup::kDefaultLoadAddress + 0x2000;
  function->set_code_ranges(AddressRanges(AddressRange(kFunctionBeginAddr, kFunctionEndAddr)));
  SymbolTestParentSetter function_parent(function, ns);

  // Function parameters.
  auto param_value =
      MakeVariableForTest("value", int32_type, kFunctionBeginAddr, kFunctionEndAddr, var_loc);
  auto param_other =
      MakeVariableForTest("other_param", int32_type, kFunctionBeginAddr, kFunctionEndAddr, var_loc);
  function->set_parameters({LazySymbol(param_value), LazySymbol(param_other)});

  // Function local variables.
  auto var_value =
      MakeVariableForTest("value", int32_type, kFunctionBeginAddr, kFunctionEndAddr, var_loc);
  auto var_other = MakeVariableForTest("function_local", int32_type, kFunctionBeginAddr,
                                       kFunctionEndAddr, var_loc);
  function->set_variables({LazySymbol(var_value), LazySymbol(var_other)});
  FindNameContext function_context(&setup.process(), symbol_context, function.get());

  // Inner block.
  uint64_t kBlockBeginAddr = ProcessSymbolsTestSetup::kDefaultLoadAddress + 0x1100;
  uint64_t kBlockEndAddr = ProcessSymbolsTestSetup::kDefaultLoadAddress + 0x1200;
  auto block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  block->set_code_ranges(AddressRanges(AddressRange(kBlockBeginAddr, kBlockEndAddr)));
  SymbolTestParentSetter block_parent(block, function);
  function->set_inner_blocks({LazySymbol(block)});

  // Inner block variables.
  auto block_value =
      MakeVariableForTest("value", int32_type, kBlockBeginAddr, kBlockEndAddr, var_loc);
  auto block_other =
      MakeVariableForTest("block_local", int32_type, kBlockBeginAddr, kBlockEndAddr, var_loc);
  block->set_variables({LazySymbol(block_value), LazySymbol(block_other)});
  FindNameContext block_context(&setup.process(), symbol_context, block.get());

  // ACTUAL TEST CODE ------------------------------------------------------------------------------

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
  EXPECT_EQ(var_value->GetAssignedName(), found.GetName().GetFullNameNoQual());

  // Find "::value" should match nothing.
  ParsedIdentifier value_global_ident(IdentifierQualification::kGlobal,
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

  // Find "block_local" in the block should be found, but in the function it should not be.
  ParsedIdentifier block_local_ident(block_other->GetAssignedName());
  found = FindName(block_context, all_kinds, block_local_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(block_other.get(), found.variable());
  EXPECT_EQ(block_other->GetAssignedName(), found.GetName().GetFullNameNoQual());
  found = FindName(function_context, all_kinds, block_local_ident);
  EXPECT_FALSE(found);

  // Finding the other function parameter in the block should work.
  ParsedIdentifier other_param_ident(param_other->GetAssignedName());
  found = FindName(block_context, all_kinds, other_param_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(param_other.get(), found.variable());

  // Look up the variable "ns::ns_value" using the name "ns_value" (no namespace) from within the
  // context of the "ns::function()" function. The namespace of the function should be implicitly
  // picked up.
  ParsedIdentifier ns_value_ident(kNsVarName);
  found = FindName(block_context, all_kinds, ns_value_ident);
  EXPECT_TRUE(found);
  EXPECT_EQ(ns_value.var.get(), found.variable());
  EXPECT_EQ(kNsVarName, found.GetName().GetFullNameNoQual());

  // Loop up the global "ns_value" var with no global symbol context. This should fail and not
  // crash.
  FindNameContext block_no_modules_context;
  block_no_modules_context.block = block.get();
  found = FindName(block_no_modules_context, all_kinds, ns_value_ident);
  EXPECT_FALSE(found);
}

// This test only tests for finding object members. It doesn't set up the index which might find
// types, that's tested by FindIndexedName.
TEST(FindName, FindMember) {
  DerivedClassTestSetup d;

  FindNameContext context;  // Empty context = local and object vars only.
  FindNameOptions exact_var(FindNameOptions::kAllKinds);

  // The two base classes each have a "b" member.
  ParsedIdentifier b_ident("b");

  // Finding one member "b" should find the first one (Base1) because the options find the first
  // match by default.
  std::vector<FoundName> results;
  FindMember(context, exact_var, d.derived_type.get(), b_ident, nullptr, &results);
  ASSERT_EQ(1u, results.size());
  ASSERT_EQ(FoundName::kMemberVariable, results[0].kind());
  EXPECT_EQ(d.kBase1Offset, results[0].member().object_path().BaseOffsetInDerived());
  EXPECT_EQ("b", results[0].GetName().GetFullNameNoQual());

  // Increase the limit, it should find both in order of Base1, Base2.
  results.clear();
  exact_var.max_results = 100;
  FindMember(context, exact_var, d.derived_type.get(), b_ident, nullptr, &results);
  ASSERT_EQ(2u, results.size());
  ASSERT_EQ(FoundName::kMemberVariable, results[0].kind());
  ASSERT_EQ(FoundName::kMemberVariable, results[1].kind());
  EXPECT_EQ(d.kBase1Offset, results[0].member().object_path().BaseOffsetInDerived());
  EXPECT_EQ(d.kBase2Offset, results[1].member().object_path().BaseOffsetInDerived());
}

TEST(FindName, FindAnonUnion) {
  // Makes this type:
  //   struct Outer {
  //     union Union {
  //       int inner;
  //     };
  //   }
  // and makes sure that we can evaluate "outer.inner", transparently going into the anonymous
  // union.

  auto int_type = MakeInt32Type();
  constexpr uint32_t kInnerOffset = 4;  // Offset of "inner" inside the union.

  constexpr char kInnerName[] = "inner";
  auto union_type = MakeCollectionTypeWithOffset(DwarfTag::kUnionType, "", kInnerOffset,
                                                 {{kInnerName, int_type}});

  constexpr uint32_t kUnionOffset = 2;  // Offset of the union inside "Outer".
  auto outer_type = MakeCollectionTypeWithOffset(DwarfTag::kStructureType, "Outer", kUnionOffset,
                                                 {{"", union_type}});

  constexpr uint8_t kIntValue = 42;
  ExprValue value(outer_type, {0, 0, 0, 0, 0, 0,      // Padding: kInnerOffset + kUnionOffset bytes.
                               kIntValue, 0, 0, 0});  // 32-bit integer little-endian.

  FindNameContext context;  // Empty context = local and object vars only.
  FindNameOptions exact_var(FindNameOptions::kAllKinds);
  std::vector<FoundName> result;
  FindMember(context, exact_var, outer_type.get(), ParsedIdentifier(kInnerName), nullptr, &result);
  ASSERT_EQ(1u, result.size());

  // The found value should be at the correct offset, accounting for both the union and integer
  // offsets.
  EXPECT_EQ(kInnerOffset + kUnionOffset, result[0].member().GetDataMemberOffset());
  EXPECT_EQ(kInnerName, result[0].member().data_member()->GetAssignedName());
}

// This only tests the ModuleSymbols and function naming integration, the details of the index
// searching are tested by FindGlobalNameInModule()
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
  auto module_symbols1 = fxl::MakeRefCounted<MockModuleSymbols>("mod1.so");
  auto& root1 = module_symbols1->index().root();  // Root of the index for module 1.
  TestIndexedGlobalVariable global1(module_symbols1.get(), &root1, kGlobalName);
  TestIndexedGlobalVariable var1(module_symbols1.get(), &root1, kVar1Name);
  constexpr uint64_t kLoadAddress1 = 0x1000;
  SymbolContext symbol_context1(kLoadAddress1);
  setup.InjectModule("mod1", "1234", kLoadAddress1, module_symbols1);

  // Module 2.
  auto module_symbols2 = fxl::MakeRefCounted<MockModuleSymbols>("mod2.so");
  auto& root2 = module_symbols2->index().root();  // Root of the index for module 1.
  TestIndexedGlobalVariable global2(module_symbols2.get(), &root2, kGlobalName);
  TestIndexedGlobalVariable var2(module_symbols2.get(), &root2, kVar2Name);
  constexpr uint64_t kLoadAddress2 = 0x2000;
  SymbolContext symbol_context2(kLoadAddress2);
  setup.InjectModule("mod2", "5678", kLoadAddress2, module_symbols2);

  FindNameOptions all_opts(FindNameOptions::kAllKinds);
  std::vector<FoundName> found;

  // Searching for "global" in module1's context should give the global in that module.
  FindNameContext mod1_context(&setup.process(), symbol_context1);
  FindIndexedName(mod1_context, all_opts, ParsedIdentifier(), global_ident, true, &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global1.var.get(), found[0].variable());

  // Searching for "global" in module2's context should give the global in that module.
  found.clear();
  FindNameContext mod2_context(&setup.process(), symbol_context2);
  FindIndexedName(mod2_context, all_opts, ParsedIdentifier(), global_ident, true, &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global2.var.get(), found[0].variable());

  // Searching for "var1" in module2's context should still find it even though its in the other
  // module.
  found.clear();
  FindIndexedName(mod2_context, all_opts, ParsedIdentifier(), var1_ident, true, &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(var1.var.get(), found[0].variable());

  // Searching for "var2" with only target-level symbols should still find it.
  found.clear();
  FindIndexedName(FindNameContext(&setup.target()), all_opts, ParsedIdentifier(), var2_ident, true,
                  &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(var2.var.get(), found[0].variable());
}

TEST(FindName, FindIndexedNameInModule) {
  auto module_symbols = fxl::MakeRefCounted<MockModuleSymbols>("test.so");
  auto& index_root = module_symbols->index().root();  // Root of the index.

  const char kVarName[] = "var";
  const char kNsName[] = "ns";

  FindNameOptions all_opts(FindNameOptions::kAllKinds);
  std::vector<FoundName> found;

  // Make a global variable in the toplevel namespace.
  TestIndexedGlobalVariable global(module_symbols.get(), &index_root, kVarName);

  ParsedIdentifier var_ident(kVarName);
  FindIndexedNameInModule(all_opts, module_symbols.get(), ParsedIdentifier(), var_ident, true,
                          &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global.var.get(), found[0].variable());

  // Say we're in some nested namespace and search for the same name. It should find the variable in
  // the upper namespace.
  ParsedIdentifier nested_ns(kNsName);
  found.clear();
  FindIndexedNameInModule(all_opts, module_symbols.get(), nested_ns, var_ident, true, &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global.var.get(), found[0].variable());

  // Add a variable in the nested namespace with the same name.
  auto ns_node = index_root.AddChild(IndexNode::Kind::kNamespace, kNsName, IndexNode::DieRef());
  TestIndexedGlobalVariable ns(module_symbols.get(), ns_node, kVarName);

  // Re-search for the same name in the nested namespace, it should get the nested one first.
  found.clear();
  FindIndexedNameInModule(all_opts, module_symbols.get(), nested_ns, var_ident, true, &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(ns.var.get(), found[0].variable());

  // Now do the same search but globally qualify the input "::var" which should match only the
  // toplevel one.
  ParsedIdentifier var_global_ident(IdentifierQualification::kGlobal,
                                    ParsedIdentifierComponent(kVarName));
  found.clear();
  FindIndexedNameInModule(all_opts, module_symbols.get(), nested_ns, var_global_ident, true,
                          &found);
  ASSERT_EQ(1u, found.size());
  EXPECT_EQ(global.var.get(), found[0].variable());
  EXPECT_EQ(kVarName, found[0].GetName().GetFullNameNoQual());
}

TEST(FindName, FindTypeName) {
  ProcessSymbolsTestSetup setup;
  MockModuleSymbols* module_symbols = setup.InjectMockModule();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);
  auto& index_root = module_symbols->index().root();

  // Note space in "> >" which is how Clang likes to represent this.
  const char kGlobalTypeName[] = "GlobalType<std::char_traits<char> >";
  const char kChildTypeName[] = "ChildType<std::char_traits<char> >";

  // Global class name.
  ParsedIdentifier global_type_name(kGlobalTypeName);
  auto global_type = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  global_type->set_assigned_name(kGlobalTypeName);
  TestIndexedSymbol global_indexed(module_symbols, &index_root, kGlobalTypeName, global_type);

  // Child type definition inside the global class name. Currently types don't have child types and
  // everything is found via the index.
  ParsedIdentifier child_type_name(kChildTypeName);
  ParsedIdentifier full_child_type_name;
  Err err = ExprParser::ParseIdentifier(
      "GlobalType<std::char_traits<char> >::ChildType<std::char_traits<char> >",
      &full_child_type_name);
  ASSERT_FALSE(err.has_error());
  auto child_type = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  child_type->set_assigned_name(kChildTypeName);
  TestIndexedSymbol child_indexed(module_symbols, global_indexed.index_node, kChildTypeName,
                                  child_type);

  // Declares a variable that points to the GlobalType. It will be the "this" pointer for the
  // function. The address range of this variable doesn't overlap the function. This means we can
  // never compute its value, but since it's syntactically in-scope, we should still be able to use
  // its type to resolve type names on the current class.
  auto global_type_ptr = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kPointerType, global_type);
  auto this_var = MakeVariableForTest("this", global_type_ptr, 0x9000, 0x9001,
                                      {llvm::dwarf::DW_OP_reg0, llvm::dwarf::DW_OP_stack_value});

  // Function as a member of GlobalType.
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("function");
  uint64_t kFunctionBeginAddr = 0x1000;
  uint64_t kFunctionEndAddr = 0x2000;
  function->set_code_ranges(AddressRanges(AddressRange(kFunctionBeginAddr, kFunctionEndAddr)));
  function->set_object_pointer(this_var);

  // This context declares a target and a block but no current module, which means the block and all
  // modules should be searched with no particular preference (most other code sets a preference so
  // this tests that less common case).
  FindNameContext function_context;
  function_context.target_symbols = &setup.target();
  function_context.block = function.get();

  // ACTUAL TEST CODE ------------------------------------------------------------------------------

  FindNameOptions all_kinds(FindNameOptions::kAllKinds);

  // Look up from the global function.
  FoundName found = FindName(function_context, all_kinds, global_type_name);
  EXPECT_TRUE(found);
  EXPECT_EQ(FoundName::kType, found.kind());
  EXPECT_EQ(global_type.get(), found.type().get());
  // This has gone through our ParsedIdentifier template canonicalization so doesn't have the
  // space between the ">>" like the input had.
  EXPECT_EQ("::GlobalType<std::char_traits<char>>", found.GetName().GetFullName());

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

  // Look up the child function by just the child name. Since the function is a member of
  // GlobalType, ChildType is a member of "this" so it should be found.
  found = FindName(function_context, all_kinds, child_type_name);
  EXPECT_TRUE(found);
  EXPECT_EQ(FoundName::kType, found.kind());
  EXPECT_EQ(child_type.get(), found.type().get());
}

TEST(FindName, FindTemplateName) {
  ProcessSymbolsTestSetup setup;
  MockModuleSymbols* module_symbols = setup.InjectMockModule();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);
  auto& index_root = module_symbols->index().root();

  // Declare two functions, one's a template, the other has the same prefix but isn't.
  const char kTemplateIntName[] = "Template<int>";
  const char kTemplateNotName[] = "TemplateNot";

  ParsedIdentifier template_int_name(kTemplateIntName);
  ParsedIdentifier template_not_name(kTemplateNotName);

  auto template_int = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  template_int->set_assigned_name(kTemplateIntName);
  TestIndexedSymbol template_int_indexed(module_symbols, &index_root, kTemplateIntName,
                                         template_int);

  auto template_not = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  template_not->set_assigned_name(kTemplateNotName);
  TestIndexedSymbol template_not_indexed(module_symbols, &index_root, kTemplateNotName,
                                         template_not);

  // Search for names globally within the target.
  FindNameContext context(&setup.target());

  FindNameOptions all_types(FindNameOptions::kAllKinds);

  // The string "Template" should be identified as one.
  ParsedIdentifier template_name("Template");
  auto found = FindName(context, all_types, template_name);
  EXPECT_TRUE(found);
  EXPECT_EQ(FoundName::kTemplate, found.kind());
  EXPECT_EQ("Template", found.GetName().GetFullName());

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

  // Prefix search for "Templ" should get both full types. Since prefix searching doesn't currently
  // work for templates, we won't get a template record. These results will need to be updated if
  // template prefix matching is added.
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

TEST(FindName, FindType) {
  ProcessSymbolsTestSetup setup;
  auto module_symbols1 = fxl::MakeRefCounted<MockModuleSymbols>("mod1.so");
  auto& index_root1 = module_symbols1->index().root();
  auto module_symbols2 = fxl::MakeRefCounted<MockModuleSymbols>("mod2.so");
  auto& index_root2 = module_symbols2->index().root();

  const char kStructName[] = "Struct";

  ParsedIdentifier struct_name(kStructName);

  // Make and index the forward declaration in module 1.
  auto fwd_decl = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  fwd_decl->set_assigned_name(kStructName);
  fwd_decl->set_is_declaration(true);
  TestIndexedSymbol fwd_decl_indexed(module_symbols1.get(), &index_root1, kStructName, fwd_decl);

  // Make and index a definition in module 2.
  auto def = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
  def->set_assigned_name(kStructName);
  def->set_byte_size(12);
  TestIndexedSymbol def_indexed(module_symbols2.get(), &index_root2, kStructName, def);

  // Set the modules as loaded.
  constexpr uint64_t kLoadAddress1 = 0x1000;
  SymbolContext symbol_context1(kLoadAddress1);
  setup.InjectModule("mod1", "1234", kLoadAddress1, module_symbols1);
  constexpr uint64_t kLoadAddress2 = 0x2000;
  SymbolContext symbol_context2(kLoadAddress2);
  setup.InjectModule("mod2", "5678", kLoadAddress2, module_symbols2);

  // Search for names starting from "mod1" so the output ordering is guaranteed.
  FindNameContext context(&setup.process(), symbol_context1);

  // Finding types should return both the forward definition and the definition.
  FindNameOptions find_types(FindNameOptions::kNoKinds);
  find_types.find_types = true;
  find_types.max_results = 100;

  std::vector<FoundName> results;
  FindName(context, find_types, struct_name, &results);
  ASSERT_EQ(2u, results.size());

  // The forward-declaration should be found first since it's in the "current" module we passed to
  // FindName.
  EXPECT_EQ(fwd_decl.get(), results[0].type().get());
  EXPECT_EQ(def.get(), results[1].type().get());

  // Now find only definitions.
  FindNameOptions find_type_defs(FindNameOptions::kNoKinds);
  find_type_defs.find_type_defs = true;
  find_type_defs.max_results = 100;

  // Should find only the definition now.
  results.clear();
  FindName(context, find_type_defs, struct_name, &results);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(def.get(), results[0].type().get());
}

TEST(FindName, FindNamespace) {
  ProcessSymbolsTestSetup setup;
  MockModuleSymbols* module_symbols = setup.InjectMockModule();

  auto& index_root = module_symbols->index().root();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);
  FindNameContext context(&setup.process(), symbol_context);

  const char kStd[] = "std";
  index_root.AddChild(IndexNode::Kind::kNamespace, kStd);

  const char kStar[] = "star";
  auto* star_ns = index_root.AddChild(IndexNode::Kind::kNamespace, kStar);

  // star::internal
  const char kInternal[] = "internal";
  star_ns->AddChild(IndexNode::Kind::kNamespace, kInternal);

  FindNameOptions find_ns(FindNameOptions::kNoKinds);
  find_ns.find_namespaces = true;
  find_ns.max_results = 100;

  // Find the "std" namespace.
  std::vector<FoundName> results;
  FindName(context, find_ns, ParsedIdentifier(kStd), &results);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(FoundName::kNamespace, results[0].kind());
  EXPECT_EQ(kStd, results[0].GetName().GetFullName());

  // Find "s..." namespaces by prefix.
  FindNameOptions find_ns_prefix = find_ns;
  find_ns_prefix.how = FindNameOptions::kPrefix;
  results.clear();
  FindName(context, find_ns_prefix, ParsedIdentifier("s"), &results);
  ASSERT_EQ(2u, results.size());
  // Results can be in either order.
  EXPECT_TRUE(
      (results[0].GetName().GetFullName() == kStd && results[1].GetName().GetFullName() == kStar) ||
      (results[0].GetName().GetFullName() == kStar && results[1].GetName().GetFullName() == kStd));

  // Find the "star::i" namespace by prefix.
  ParsedIdentifier star_internal_prefix;
  ASSERT_TRUE(ExprParser::ParseIdentifier("star::i", &star_internal_prefix).ok());
  results.clear();
  FindName(context, find_ns_prefix, star_internal_prefix, &results);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ("star::internal", results[0].GetName().GetFullName());
}

// A symbol should be found in any namespace with the "all namespaces" flag set.
TEST(FindName, FindRecursiveNamespace) {
  ProcessSymbolsTestSetup setup;
  MockModuleSymbols* module_symbols = setup.InjectMockModule();

  auto& index_root = module_symbols->index().root();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);
  FindNameContext context(&setup.process(), symbol_context);

  // Make several functions
  //
  // - ::Foo()
  // - ::std::Foo()
  // - ::std::bar::Foo()
  // - ::std::$anon::Foo()

  const char kStdName[] = "std";
  auto std_ns_symbol = fxl::MakeRefCounted<Namespace>(kStdName);
  auto std_ns = index_root.AddChild(IndexNode::Kind::kNamespace, kStdName);

  const char kBarName[] = "bar";
  auto std_bar_ns_symbol = fxl::MakeRefCounted<Namespace>(kBarName);
  SymbolTestParentSetter std_bar_ns_symbol_parent(std_bar_ns_symbol, std_ns_symbol);
  auto std_bar_ns = std_ns->AddChild(IndexNode::Kind::kNamespace, kBarName);

  auto std_anon_ns_symbol = fxl::MakeRefCounted<Namespace>(std::string());
  SymbolTestParentSetter std_anon_ns_symbol_parent(std_anon_ns_symbol, std_ns_symbol);
  auto std_anon_ns = std_ns->AddChild(IndexNode::Kind::kNamespace, "");

  // ::Foo().
  const char kFooName[] = "Foo";
  auto foo = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  foo->set_assigned_name(kFooName);
  TestIndexedSymbol foo_indexed(module_symbols, &index_root, kFooName, foo);

  // ::std::Foo().
  auto std_foo = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  SymbolTestParentSetter std_foo_parent(std_foo, std_ns_symbol);
  std_foo->set_assigned_name(kFooName);
  TestIndexedSymbol std_foo_indexed(module_symbols, std_ns, kFooName, std_foo);

  // ::std::bar::Foo().
  auto std_bar_foo = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  SymbolTestParentSetter std_bar_foo_parent(std_bar_foo, std_bar_ns_symbol);
  std_bar_foo->set_assigned_name(kFooName);
  TestIndexedSymbol std_bar_foo_indexed(module_symbols, std_bar_ns, kFooName, std_bar_foo);

  // ::std::$anon::Foo().
  auto std_anon_foo = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  SymbolTestParentSetter std_anon_foo_parent(std_anon_foo, std_anon_ns_symbol);
  std_anon_foo->set_assigned_name(kFooName);
  TestIndexedSymbol std_anon_foo_indexed(module_symbols, std_anon_ns, kFooName, std_anon_foo);

  // Search for "Foo" in all namespaces.
  ParsedIdentifier foo_ident((ParsedIdentifierComponent(kFooName)));
  FindNameOptions opts(FindNameOptions::kAllKinds);
  opts.max_results = 100;  // Want everything.
  opts.search_mode = FindNameOptions::kAllNamespaces;
  std::vector<FoundName> results;
  FindName(context, opts, foo_ident, &results);

  // It should have found all 3 Foo's in order.
  ASSERT_EQ(4u, results.size());
  EXPECT_EQ(foo.get(), results[0].function().get());
  EXPECT_EQ(std_foo.get(), results[1].function().get());
  EXPECT_EQ(std_anon_foo.get(), results[2].function().get());
  EXPECT_EQ(std_bar_foo.get(), results[3].function().get());

  // Now find by prefix recursively.
  FindNameOptions prefix_opts = opts;
  prefix_opts.how = FindNameOptions::kPrefix;
  results.clear();
  FindName(context, prefix_opts, ParsedIdentifier(ParsedIdentifierComponent("F")), &results);

  // Should have found the same matches.
  ASSERT_EQ(4u, results.size());
  EXPECT_EQ(foo.get(), results[0].function().get());
  EXPECT_EQ(std_foo.get(), results[1].function().get());
  EXPECT_EQ(std_anon_foo.get(), results[2].function().get());
  EXPECT_EQ(std_bar_foo.get(), results[3].function().get());

  // Find "bar::Foo" should find only the one match, using the implicit toplevel namespace.
  ParsedIdentifier bar_foo;
  bar_foo.AppendComponent(ParsedIdentifierComponent("bar"));
  bar_foo.AppendComponent(ParsedIdentifierComponent("Foo"));
  results.clear();
  FindName(context, opts, bar_foo, &results);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(std_bar_foo.get(), results[0].function().get());

  // Find "::Foo" should only find the toplevel one, even with implicit namespace searching.
  ParsedIdentifier abs_foo(IdentifierQualification::kGlobal, ParsedIdentifierComponent(kFooName));
  results.clear();
  FindName(context, opts, abs_foo, &results);
  ASSERT_EQ(1u, results.size());
  EXPECT_EQ(foo.get(), results[0].function().get());

  // Find "::std::Foo" should find both ::std::Foo and the anonymous namespace one.
  ParsedIdentifier abs_std_foo(IdentifierQualification::kGlobal,
                               ParsedIdentifierComponent(kStdName));
  abs_std_foo.AppendComponent(ParsedIdentifierComponent(kFooName));
  results.clear();
  FindName(context, opts, abs_std_foo, &results);
  ASSERT_EQ(2u, results.size());
  EXPECT_EQ(std_foo.get(), results[0].function().get());
  EXPECT_EQ(std_anon_foo.get(), results[1].function().get());
}

}  // namespace zxdb
