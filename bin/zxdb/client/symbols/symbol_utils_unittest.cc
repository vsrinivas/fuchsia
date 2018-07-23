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

// This implicitly tests GetSymbolScopePrefix and SymbolScopeToPrefixString
// also, as well as GetFullyQualifiedSymbolName as an extra bonus.
TEST(SymbolUtils, SymbolScope) {
  fxl::RefPtr<Namespace> ns1 = fxl::MakeRefCounted<Namespace>();
  ns1->set_assigned_name("ns1");
  EXPECT_EQ("", GetSymbolScopePrefix(ns1.get()));
  EXPECT_EQ("ns1", GetFullyQualifiedSymbolName(ns1.get()));

  // Nested anonymous namespace.
  fxl::RefPtr<Namespace> ns2 = fxl::MakeRefCounted<Namespace>();
  ns2->set_parent(LazySymbol(ns1));
  EXPECT_EQ("ns1::", GetSymbolScopePrefix(ns2.get()));
  EXPECT_EQ("ns1::(anon)", GetFullyQualifiedSymbolName(ns2.get()));

  // Struct inside anonymous namespace.
  fxl::RefPtr<StructClass> st =
      fxl::MakeRefCounted<StructClass>(Symbol::kTagStructureType);
  st->set_parent(LazySymbol(ns2));
  st->set_assigned_name("Struct");
  EXPECT_EQ("ns1::(anon)::", GetSymbolScopePrefix(st.get()));
  EXPECT_EQ("ns1::(anon)::Struct", GetFullyQualifiedSymbolName(st.get()));

  // Data member inside structure.
  fxl::RefPtr<DataMember> dm = fxl::MakeRefCounted<DataMember>();
  dm->set_parent(LazySymbol(st));
  dm->set_assigned_name("data_");
  EXPECT_EQ("ns1::(anon)::Struct::", GetSymbolScopePrefix(dm.get()));
  EXPECT_EQ("ns1::(anon)::Struct::data_",
            GetFullyQualifiedSymbolName(dm.get()));
}

// Tests some symbol scope descriptions with respect to functions.
TEST(SymbolUtils, SymbolScopeFunctions) {
  // Outer namespace.
  fxl::RefPtr<Namespace> ns = fxl::MakeRefCounted<Namespace>();
  ns->set_assigned_name("ns");

  // Function definition inside namespace.
  fxl::RefPtr<Function> fn = fxl::MakeRefCounted<Function>();
  fn->set_parent(LazySymbol(ns));
  fn->set_assigned_name("Function");
  EXPECT_EQ("ns::", GetSymbolScopePrefix(fn.get()));
  EXPECT_EQ("ns::Function", GetFullyQualifiedSymbolName(fn.get()));

  // Type defined inside the function, this will likely need to change in the
  // future to be qualified based on the function name. Currently these are
  // not qualified.
  fxl::RefPtr<Variable> var =
      fxl::MakeRefCounted<Variable>(Symbol::kTagVariable);
  var->set_parent(LazySymbol(fn));
  var->set_assigned_name("var");
  EXPECT_EQ("", GetSymbolScopePrefix(var.get()));
  EXPECT_EQ("var", GetFullyQualifiedSymbolName(var.get()));
}

}  // namespace zxdb
