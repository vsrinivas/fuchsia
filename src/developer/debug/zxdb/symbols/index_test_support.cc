// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/index_test_support.h"

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"

namespace zxdb {

namespace {

IndexNode::Kind KindForSymbol(const fxl::RefPtr<Symbol>& sym) {
  if (sym->As<Type>())
    return IndexNode::Kind::kType;
  if (sym->As<Namespace>())
    return IndexNode::Kind::kNamespace;
  if (sym->As<Function>())
    return IndexNode::Kind::kFunction;
  if (sym->As<Variable>())
    return IndexNode::Kind::kVar;

  FX_NOTREACHED();
  return IndexNode::Kind::kVar;
}

}  // namespace

int TestIndexedSymbol::next_die_ref = 1;

TestIndexedSymbol::TestIndexedSymbol(MockModuleSymbols* mod_sym, IndexNode* index_parent,
                                     const std::string& name, fxl::RefPtr<Symbol> sym)
    : die_ref(IndexNode::SymbolRef::kDwarf, next_die_ref++),
      index_node(index_parent->AddChild(KindForSymbol(sym), name.c_str(), die_ref)),
      symbol(std::move(sym)) {
  mod_sym->AddSymbolRef(die_ref, symbol);
}

TestIndexedGlobalVariable::TestIndexedGlobalVariable(MockModuleSymbols* mod_sym,
                                                     IndexNode* index_parent,
                                                     const std::string& var_name)
    : TestIndexedSymbol(mod_sym, index_parent, var_name,
                        MakeVariableForTest(var_name, MakeInt32Type(), 0x100, 0x200, DwarfExpr())),
      var(symbol->As<Variable>()) {}

}  // namespace zxdb
