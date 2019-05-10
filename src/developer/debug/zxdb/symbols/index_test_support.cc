// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/index_test_support.h"

#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"

namespace zxdb {

namespace {

ModuleSymbolIndexNode::RefType RefTypeForSymbol(
    const fxl::RefPtr<Symbol>& sym) {
  if (sym->AsType())
    return ModuleSymbolIndexNode::RefType::kType;
  if (sym->AsNamespace())
    return ModuleSymbolIndexNode::RefType::kNamespace;
  if (sym->AsFunction())
    return ModuleSymbolIndexNode::RefType::kFunction;
  if (sym->AsVariable())
    return ModuleSymbolIndexNode::RefType::kVariable;

  FXL_NOTREACHED();
  return ModuleSymbolIndexNode::RefType::kVariable;
}

}  // namespace

int TestIndexedSymbol::next_die_ref = 1;

TestIndexedSymbol::TestIndexedSymbol(MockModuleSymbols* mod_sym,
                                     ModuleSymbolIndexNode* index_parent,
                                     const std::string& name,
                                     fxl::RefPtr<Symbol> sym)
    : die_ref(RefTypeForSymbol(sym), next_die_ref++),
      index_node(index_parent->AddChild(name)),
      symbol(std::move(sym)) {
  index_node->AddDie(die_ref);
  mod_sym->AddDieRef(die_ref, symbol);
}

TestIndexedGlobalVariable::TestIndexedGlobalVariable(
    MockModuleSymbols* mod_sym, ModuleSymbolIndexNode* index_parent,
    const std::string& var_name)
    : TestIndexedSymbol(mod_sym, index_parent, var_name,
                        MakeVariableForTest(var_name, MakeInt32Type(), 0x100,
                                            0x200, std::vector<uint8_t>())),
      var(symbol->AsVariable()) {}

}  // namespace zxdb
