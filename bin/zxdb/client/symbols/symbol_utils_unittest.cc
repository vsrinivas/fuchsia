// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/symbol_utils.h"
#include "garnet/bin/zxdb/client/symbols/data_member.h"
#include "garnet/bin/zxdb/client/symbols/function.h"
#include "garnet/bin/zxdb/client/symbols/namespace.h"
#include "garnet/bin/zxdb/client/symbols/struct_class.h"
#include "garnet/bin/zxdb/client/symbols/variable.h"
#include "gtest/gtest.h"

namespace zxdb {

// This implicitly tests GetSymbolScopePrefix and SymbolScopeToPrefixString.
// And checks Symbol::GetFullName() for those types.
TEST(SymbolUtils, SymbolScope) {
  fxl::RefPtr<Namespace> ns1 = fxl::MakeRefCounted<Namespace>();
  ns1->set_assigned_name("ns1");
  EXPECT_EQ("", GetSymbolScopePrefix(ns1.get()));
  EXPECT_EQ("ns1", ns1->GetFullName());

  // Nested anonymous namespace.
  fxl::RefPtr<Namespace> ns2 = fxl::MakeRefCounted<Namespace>();
  ns2->set_parent(LazySymbol(ns1));
  EXPECT_EQ("ns1::", GetSymbolScopePrefix(ns2.get()));
  EXPECT_EQ("ns1::(anon)", ns2->GetFullName());

  // Struct inside anonymous namespace.
  fxl::RefPtr<StructClass> st =
      fxl::MakeRefCounted<StructClass>(Symbol::kTagStructureType);
  st->set_parent(LazySymbol(ns2));
  st->set_assigned_name("Struct");
  EXPECT_EQ("ns1::(anon)::", GetSymbolScopePrefix(st.get()));
  EXPECT_EQ("ns1::(anon)::Struct", st->GetFullName());

  // Data member inside structure.
  fxl::RefPtr<DataMember> dm = fxl::MakeRefCounted<DataMember>();
  dm->set_parent(LazySymbol(st));
  dm->set_assigned_name("data_");
  EXPECT_EQ("ns1::(anon)::Struct::", GetSymbolScopePrefix(dm.get()));
  EXPECT_EQ("ns1::(anon)::Struct::data_", dm->GetFullName());
}

// Tests some symbol scope descriptions with respect to functions.
TEST(SymbolUtils, SymbolScopeFunctions) {
  // Outer namespace.
  fxl::RefPtr<Namespace> ns = fxl::MakeRefCounted<Namespace>();
  ns->set_assigned_name("ns");

  // Function definition inside namespace.
  // TODO(brettw) these will need to be updated when function parameter types
  // are added to the name.
  fxl::RefPtr<Function> fn = fxl::MakeRefCounted<Function>();
  fn->set_parent(LazySymbol(ns));
  fn->set_assigned_name("Function");
  EXPECT_EQ("ns::", GetSymbolScopePrefix(fn.get()));
  EXPECT_EQ("ns::Function()", fn->GetFullName());

  // Lexical scope inside the function. This should not appear in names.
  fxl::RefPtr<CodeBlock> block =
      fxl::MakeRefCounted<CodeBlock>(Symbol::kTagLexicalBlock);
  block->set_parent(LazySymbol(fn));
  EXPECT_EQ("ns::Function()::", GetSymbolScopePrefix(block.get()));
  EXPECT_EQ("", block->GetFullName());

  // Type defined inside the function is qualified by the function name. This
  // format matches GDB and LLDB.
  fxl::RefPtr<StructClass> sc =
      fxl::MakeRefCounted<StructClass>(Symbol::kTagStructureType);
  sc->set_parent(LazySymbol(block));
  sc->set_assigned_name("Struct");
  EXPECT_EQ("ns::Function()::", GetSymbolScopePrefix(sc.get()));
  EXPECT_EQ("ns::Function()::Struct", sc->GetFullName());

  // Variable defined inside the function. Currently these are qualified with
  // the function name like the types above. But this may need to change
  // depending on how these are surfaced to the user. Qualifying local
  // variables seems weird and we likely don't want this, but it currently
  // works that way because it falls out of the algorithm. However, the
  // local variable printer may well not need this code path.
  fxl::RefPtr<Variable> var =
      fxl::MakeRefCounted<Variable>(Symbol::kTagVariable);
  var->set_parent(LazySymbol(block));
  var->set_assigned_name("var");
  EXPECT_EQ("ns::Function()::", GetSymbolScopePrefix(var.get()));
  EXPECT_EQ("ns::Function()::var", var->GetFullName());
}

}  // namespace zxdb
