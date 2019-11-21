// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/symbol.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/compile_unit.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/mock_module_symbols.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/symbol_test_parent_setter.h"

namespace zxdb {

// Implicitly tests GetModuleSymbols and GetCompileUnit as well.
TEST(Symbol, GetSymbolContext) {
  ProcessSymbolsTestSetup setup;
  MockModuleSymbols* mock_module_symbols = setup.InjectMockModule();
  SymbolContext input_context(ProcessSymbolsTestSetup::kDefaultLoadAddress);

  // Set up a chain:
  //  - compile_unit
  //    - namespace
  //      - function
  auto compile_unit =
      fxl::MakeRefCounted<CompileUnit>(mock_module_symbols->GetWeakPtr(), DwarfLang::kC, "file.cc");

  auto ns = fxl::MakeRefCounted<Namespace>("ns");
  SymbolTestParentSetter ns_parent(ns, compile_unit);

  auto func = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);

  // This scope clears the function's parent when it exits.
  {
    SymbolTestParentSetter func_parent(func, ns);

    // The function should be able to provide the original symbol context.
    SymbolContext context = func->GetSymbolContext(&setup.process());
    EXPECT_EQ(input_context, context);
  }
  // The function's parent is now unset.

  // The chain to the unit might be broken if the module is deleted out from under us (in
  // production this happens the parent can't be recreated from the factory, but in this test we
  // cleared it explicitly).
  SymbolContext context = func->GetSymbolContext(&setup.process());
  EXPECT_TRUE(context.is_relative());
}

}  // namespace zxdb
