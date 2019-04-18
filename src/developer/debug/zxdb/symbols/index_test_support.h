// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INDEX_TEST_SUPPORT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INDEX_TEST_SUPPORT_H_

#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/module_symbol_index_node.h"
#include "src/developer/debug/zxdb/symbols/variable.h"

namespace zxdb {

// Creates a symbol in the index and the mock module symbols.
struct TestIndexedSymbol {
  // Index of the next DieRef to generated. This ensures the generated IDs are
  // unique.
  static int next_die_ref;

  TestIndexedSymbol(MockModuleSymbols* mod_sym,
                    ModuleSymbolIndexNode* index_parent,
                    const std::string& name, fxl::RefPtr<Symbol> sym);

  // The DieRef links the index and the entry injected into the ModuleSymbols.
  ModuleSymbolIndexNode::DieRef die_ref;

  // Place where this variable is indexed.
  ModuleSymbolIndexNode* index_node;

  fxl::RefPtr<Symbol> symbol;
};

// Creates a global variable that's inserted into the index and the mock
// ModuleSymbols.
struct TestIndexedGlobalVariable : public TestIndexedSymbol {
  TestIndexedGlobalVariable(MockModuleSymbols* mod_sym,
                            ModuleSymbolIndexNode* index_parent,
                            const std::string& var_name);

  // The variable itself.
  fxl::RefPtr<Variable> var;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_INDEX_TEST_SUPPORT_H_
