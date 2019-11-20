// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol_utils.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/collection.h"
#include "src/developer/debug/zxdb/symbols/data_member.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

// This tests GetSymbolScopePrefix and checks Symbol::GetFullName() for those types.
TEST(SymbolUtils, GetSymbolScopePrefix) {
  fxl::RefPtr<Namespace> ns1 = fxl::MakeRefCounted<Namespace>();
  ns1->set_assigned_name("ns1");
  EXPECT_EQ("::", GetSymbolScopePrefix(ns1.get()).GetFullName());
  EXPECT_EQ("ns1", ns1->GetFullName());

  // Nested anonymous namespace.
  fxl::RefPtr<Namespace> ns2 = fxl::MakeRefCounted<Namespace>();
  SymbolTestParentSetter ns2_parent(ns2, ns1);
  EXPECT_EQ("::ns1", GetSymbolScopePrefix(ns2.get()).GetFullName());
  EXPECT_EQ("ns1::$anon", ns2->GetFullName());

  // Struct inside anonymous namespace.
  fxl::RefPtr<Collection> st = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  SymbolTestParentSetter st_parent(st, ns2);
  st->set_assigned_name("Struct");
  EXPECT_EQ("::ns1::$anon", GetSymbolScopePrefix(st.get()).GetFullName());
  EXPECT_EQ("ns1::$anon::Struct", st->GetFullName());

  // Data member inside structure.
  fxl::RefPtr<DataMember> dm = fxl::MakeRefCounted<DataMember>();
  SymbolTestParentSetter dm_parent(dm, st);
  dm->set_assigned_name("data_");
  EXPECT_EQ("::ns1::$anon::Struct", GetSymbolScopePrefix(dm.get()).GetFullName());
  EXPECT_EQ("ns1::$anon::Struct::data_", dm->GetFullName());
}

// Tests some symbol scope descriptions with respect to functions.
TEST(SymbolUtils, SymbolScopeFunctions) {
  // Outer namespace.
  fxl::RefPtr<Namespace> ns = fxl::MakeRefCounted<Namespace>();
  ns->set_assigned_name("ns");

  // Function definition inside namespace.
  fxl::RefPtr<Function> fn = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  SymbolTestParentSetter fn_parent(fn, ns);
  fn->set_assigned_name("Function");
  EXPECT_EQ("::ns", GetSymbolScopePrefix(fn.get()).GetFullName());
  EXPECT_EQ("ns::Function", fn->GetFullName());

  // Lexical scope inside the function. This should not appear in names.
  // TODO(brettw) these nested function scopes should include paramter names.
  fxl::RefPtr<CodeBlock> block = fxl::MakeRefCounted<CodeBlock>(DwarfTag::kLexicalBlock);
  SymbolTestParentSetter block_parent(block, fn);
  EXPECT_EQ("::ns::Function", GetSymbolScopePrefix(block.get()).GetFullName());
  EXPECT_EQ("", block->GetFullName());

  // Type defined inside the function is qualified by the function name. This format matches GDB and
  // LLDB.
  fxl::RefPtr<Collection> sc = fxl::MakeRefCounted<Collection>(DwarfTag::kStructureType);
  SymbolTestParentSetter sc_parent(sc, block);
  sc->set_assigned_name("Struct");
  EXPECT_EQ("::ns::Function", GetSymbolScopePrefix(sc.get()).GetFullName());
  EXPECT_EQ("ns::Function::Struct", sc->GetFullName());

  // Variable defined inside the function. Currently these are qualified with the function name like
  // the types above. But this may need to change depending on how these are surfaced to the user.
  // Qualifying local variables seems weird and we likely don't want this, but it currently works
  // that way because it falls out of the algorithm. However, the local variable printer may well
  // not need this code path.
  fxl::RefPtr<Variable> var = fxl::MakeRefCounted<Variable>(DwarfTag::kVariable);
  SymbolTestParentSetter var_parent(var, block);
  var->set_assigned_name("var");
  EXPECT_EQ("::ns::Function", GetSymbolScopePrefix(var.get()).GetFullName());
  EXPECT_EQ("ns::Function::var", var->GetFullName());
}

}  // namespace zxdb
