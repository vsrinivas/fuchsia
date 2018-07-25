// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_symbol_factory.h"
#include "garnet/bin/zxdb/client/string_util.h"
#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/data_member.h"
#include "garnet/bin/zxdb/client/symbols/dwarf_test_util.h"
#include "garnet/bin/zxdb/client/symbols/function.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"
#include "garnet/bin/zxdb/client/symbols/struct_class.h"
#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "garnet/bin/zxdb/client/symbols/test_symbol_module.h"
#include "garnet/bin/zxdb/client/symbols/variable.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

// Returns the function symbol with the given name. The name is assumed to
// exit as this function will EXPECT_* it to be valid. Returns empty refptr on
// failure.
fxl::RefPtr<const Function> GetFunctionWithName(ModuleSymbolsImpl& module,
                                                const std::string& name) {
  DwarfSymbolFactory* factory = module.symbol_factory();

  llvm::DWARFUnit* unit = GetUnitWithNameEndingIn(
      module.context(), module.compile_units(), "/type_test.cc");
  EXPECT_TRUE(unit);
  if (!unit)
    return fxl::RefPtr<Function>();

  // Find the GetIntPtr function.
  llvm::DWARFDie get_int_ptr_function_die = GetFirstDieOfTagAndName(
      module.context(), unit, llvm::dwarf::DW_TAG_subprogram, name);
  EXPECT_TRUE(get_int_ptr_function_die);
  if (!get_int_ptr_function_die)
    return fxl::RefPtr<Function>();

  // Should make a valid lazy reference to the function DIE.
  LazySymbol lazy_function = factory->MakeLazy(get_int_ptr_function_die);
  EXPECT_TRUE(lazy_function);
  if (!lazy_function)
    return fxl::RefPtr<Function>();

  // Deserialize to a function object.
  const Symbol* function_symbol = lazy_function.Get();
  EXPECT_EQ(Symbol::kTagSubprogram, function_symbol->tag());
  return fxl::RefPtr<const Function>(function_symbol->AsFunction());
}

}  // namespace

TEST(DwarfSymbolFactory, FunctionType) {
  ModuleSymbolsImpl module(TestSymbolModule::GetTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetIntPtr function.
  const char kGetIntPtrName[] = "GetIntPtr";
  fxl::RefPtr<const Function> function =
      GetFunctionWithName(module, kGetIntPtrName);
  ASSERT_TRUE(function);

  // Unmangled name.
  EXPECT_EQ(kGetIntPtrName, function->GetAssignedName());

  // Mangled name. This tries not to depend on the exact name mangling rules
  // while validating that it's reasonable. The mangled name shouldn't be
  // exactly the same as the unmangled name, but should at least contain it.
  EXPECT_NE(kGetIntPtrName, function->linkage_name());
  EXPECT_NE(std::string::npos, function->linkage_name().find(kGetIntPtrName));

  // Declaration location.
  EXPECT_TRUE(function->decl_line().is_valid());
  EXPECT_TRUE(StringEndsWith(function->decl_line().file(), "/type_test.cc"))
      << function->decl_line().file();
  EXPECT_EQ(10, function->decl_line().line());

  // Note: return type tested by ModifiedBaseType.
}

TEST(DwarfSymbolFactory, ModifiedBaseType) {
  ModuleSymbolsImpl module(TestSymbolModule::GetTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetIntPtr function.
  const char kGetIntPtrName[] = "GetIntPtr";
  fxl::RefPtr<const Function> function =
      GetFunctionWithName(module, kGetIntPtrName);
  ASSERT_TRUE(function);

  // Get the return type, this references a "pointer" modifier.
  EXPECT_TRUE(function->return_type().is_valid());
  const ModifiedType* ptr_mod = function->return_type().Get()->AsModifiedType();
  ASSERT_TRUE(ptr_mod) << "Tag = " << function->return_type().Get()->tag();
  EXPECT_EQ(Symbol::kTagPointerType, ptr_mod->tag());
  EXPECT_EQ("const int*", ptr_mod->GetFullName());

  // The modified type should be a "const" modifier.
  const ModifiedType* const_mod = ptr_mod->modified().Get()->AsModifiedType();
  ASSERT_TRUE(const_mod) << "Tag = " << function->return_type().Get()->tag();
  EXPECT_EQ(Symbol::kTagConstType, const_mod->tag());
  EXPECT_EQ("const int", const_mod->GetFullName());

  // The modified type should be the int base type.
  const BaseType* base = const_mod->modified().Get()->AsBaseType();
  ASSERT_TRUE(base);
  EXPECT_EQ(Symbol::kTagBaseType, base->tag());
  EXPECT_EQ("int", base->GetFullName());

  // Validate the BaseType parameters.
  EXPECT_EQ(BaseType::kBaseTypeSigned, base->base_type());
  EXPECT_EQ("int", base->GetAssignedName());
  // Try to be flexible about the size of ints on the platform.
  EXPECT_TRUE(base->byte_size() == 4 || base->byte_size() == 8);

  // This is not a bitfield.
  EXPECT_EQ(0u, base->bit_size());
  EXPECT_EQ(0u, base->bit_offset());
}

TEST(DwarfSymbolFactory, StructClass) {
  ModuleSymbolsImpl module(TestSymbolModule::GetTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the GetStruct function.
  const char kGetStruct[] = "GetStruct";
  fxl::RefPtr<const Function> function =
      GetFunctionWithName(module, kGetStruct);
  ASSERT_TRUE(function);

  // The return type should be the struct.
  auto* struct_type = function->return_type().Get()->AsStructClass();
  ASSERT_TRUE(struct_type);
  EXPECT_EQ("my_ns::Struct", struct_type->GetFullName());

  // The struct has two data members.
  ASSERT_EQ(2u, struct_type->data_members().size());

  // The first member should be "int member_a" at offset 0.
  auto* member_a = struct_type->data_members()[0].Get()->AsDataMember();
  ASSERT_TRUE(member_a);
  auto* member_a_type = member_a->type().Get()->AsType();
  EXPECT_EQ("int", member_a_type->GetFullName());
  EXPECT_EQ(0u, member_a->member_location());

  // The first member should be "int member_a". To have flexibility with the
  // compiler packing, just ensure that the offset is > 0 and a multiple of 4.
  auto* member_b = struct_type->data_members()[1].Get()->AsDataMember();
  ASSERT_TRUE(member_b);
  auto* member_b_type = member_b->type().Get()->AsType();
  EXPECT_EQ("my_ns::Struct*", member_b_type->GetFullName());
  EXPECT_LT(0u, member_b->member_location());
  EXPECT_TRUE(member_b->member_location() % 4 == 0);
}

// Tests nested code blocks, variables, and parameters.
TEST(DwarfSymbolFactory, CodeBlocks) {
  ModuleSymbolsImpl module(TestSymbolModule::GetTestFileName(), "");
  Err err = module.Load();
  EXPECT_FALSE(err.has_error()) << err.msg();

  // Find the DoStructCall function.
  const char kDoStructCall[] = "DoStructCall";
  fxl::RefPtr<const Function> function =
      GetFunctionWithName(module, kDoStructCall);
  ASSERT_TRUE(function);

  // It should have two parameters, arg1 and arg2.
  const Variable* struct_arg = nullptr;
  const Variable* int_arg = nullptr;
  ASSERT_EQ(2u, function->parameters().size());
  for (const auto& param : function->parameters()) {
    const Variable* cur_var = param.Get()->AsVariable();
    ASSERT_TRUE(cur_var);  // Each parameter should decode to a variable.
    if (cur_var->GetAssignedName() == "arg1")
      struct_arg = cur_var;
    else if (cur_var->GetAssignedName() == "arg2")
      int_arg = cur_var;
  }

  // Validate the arg1 type (const Struct&).
  ASSERT_TRUE(struct_arg);
  const Type* struct_arg_type = struct_arg->type().Get()->AsType();
  ASSERT_TRUE(struct_arg_type);
  EXPECT_EQ("const my_ns::Struct&", struct_arg_type->GetFullName());

  // Validate the arg2 type (int).
  ASSERT_TRUE(int_arg);
  const Type* int_arg_type = int_arg->type().Get()->AsType();
  ASSERT_TRUE(int_arg_type);
  EXPECT_EQ("int", int_arg_type->GetFullName());

  // The function block should have one variable (var1).
  ASSERT_EQ(1u, function->variables().size());
  const Variable* var1 = function->variables()[0].Get()->AsVariable();
  ASSERT_TRUE(var1);
  const Type* var1_type = var1->type().Get()->AsType();
  ASSERT_TRUE(var1_type);
  EXPECT_EQ("volatile int", var1_type->GetFullName());

  // There should be one child lexical scope.
  ASSERT_EQ(1u, function->inner_blocks().size());
  const CodeBlock* inner = function->inner_blocks()[0].Get()->AsCodeBlock();

  // The lexical scope should have one child variable.
  ASSERT_EQ(1u, inner->variables().size());
  const Variable* var2 = inner->variables()[0].Get()->AsVariable();
  ASSERT_TRUE(var2);
  const Type* var2_type = var2->type().Get()->AsType();
  ASSERT_TRUE(var2_type);
  EXPECT_EQ("volatile my_ns::Struct", var2_type->GetFullName());

  // The lexical scope should have no other children.
  EXPECT_TRUE(inner->inner_blocks().empty());
}

}  // namespace zxdb
