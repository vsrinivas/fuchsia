// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/resolve_type.h"

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/expr/find_name.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/dwarf_tag.h"
#include "src/developer/debug/zxdb/symbols/index_test_support.h"
#include "src/developer/debug/zxdb/symbols/modified_type.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"

namespace zxdb {

// Also tests ResolveForwardDefinition().
TEST(ResolveType, GetConcreteType) {
  ProcessSymbolsTestSetup setup;
  MockModuleSymbols* module_symbols = setup.InjectMockModule();
  SymbolContext symbol_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);

  const char kMyStructName[] = "MyStruct";

  // Make a forward declaration. It has the declaration flag set and no members or size.
  auto forward_decl = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  forward_decl->set_assigned_name(kMyStructName);
  forward_decl->set_is_declaration(true);

  // A const modification of the forward declaration.
  auto const_forward_decl = fxl::MakeRefCounted<ModifiedType>(DwarfTag::kConstType, forward_decl);

  FindNameContext find_name_context(&setup.process(), symbol_context);

  // Resolving the const forward-defined value gives the non-const version.
  auto result_type = GetConcreteType(find_name_context, const_forward_decl.get());
  EXPECT_EQ(forward_decl.get(), result_type.get());

  // Make a definition for the type. It has one 32-bit data member.
  auto def = MakeCollectionType(DwarfTag::kStructureType, kMyStructName, {{"a", MakeInt32Type()}});

  // Index the declaration of the type.
  TestIndexedSymbol indexed_def(module_symbols, &module_symbols->index().root(), kMyStructName,
                                def);

  // Now that the index exists for the type, both the const and non-const declarations should
  // resolve to the full definition.
  result_type = GetConcreteType(find_name_context, forward_decl.get());
  EXPECT_EQ(def.get(), result_type.get());
  result_type = GetConcreteType(find_name_context, const_forward_decl.get());
  EXPECT_EQ(def.get(), result_type.get());
}

}  // namespace zxdb
