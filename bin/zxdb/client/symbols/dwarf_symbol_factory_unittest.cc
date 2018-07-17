// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/dwarf_symbol_factory.h"
#include "garnet/bin/zxdb/client/string_util.h"
#include "garnet/bin/zxdb/client/symbols/base_type.h"
#include "garnet/bin/zxdb/client/symbols/dwarf_test_util.h"
#include "garnet/bin/zxdb/client/symbols/function.h"
#include "garnet/bin/zxdb/client/symbols/modified_type.h"
#include "garnet/bin/zxdb/client/symbols/module_symbols_impl.h"
#include "garnet/bin/zxdb/client/symbols/symbol.h"
#include "garnet/bin/zxdb/client/symbols/test_symbol_module.h"
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
  const char kGetIntPtrName[] = "GetIntPtr";
  llvm::DWARFDie get_int_ptr_function_die = GetFirstDieOfTagAndName(
      module.context(), unit, llvm::dwarf::DW_TAG_subprogram, kGetIntPtrName);
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
  EXPECT_EQ(kGetIntPtrName, function->name());

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
  EXPECT_EQ("const int*", ptr_mod->GetTypeName());

  // The modified type should be a "const" modifier.
  const ModifiedType* const_mod = ptr_mod->modified().Get()->AsModifiedType();
  ASSERT_TRUE(const_mod) << "Tag = " << function->return_type().Get()->tag();
  EXPECT_EQ(Symbol::kTagConstType, const_mod->tag());
  EXPECT_EQ("const int", const_mod->GetTypeName());

  // The modified type should be the int base type.
  const BaseType* base = const_mod->modified().Get()->AsBaseType();
  ASSERT_TRUE(base);
  EXPECT_EQ(Symbol::kTagBaseType, base->tag());
  EXPECT_EQ("int", base->GetTypeName());

  // Validate the BaseType parameters.
  EXPECT_EQ(BaseType::kBaseTypeSigned, base->base_type());
  EXPECT_EQ("int", base->assigned_name());
  // Try to be flexible about the size of ints on the platform.
  EXPECT_TRUE(base->byte_size() == 4 || base->byte_size() == 8);

  // This is not a bitfield.
  EXPECT_EQ(0u, base->bit_size());
  EXPECT_EQ(0u, base->bit_offset());
}

}  // namespace zxdb
