// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>
#include <sstream>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/index.h"
#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"
#include "src/developer/debug/zxdb/symbols/symbol_factory.h"
#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"

// See test_data/README.md for how to download the data required for this test and enable the build
// for it.

namespace zxdb {

namespace {

std::string GetFlutterRunnerPath() {
  return TestSymbolModule::GetTestDataDir() + "large_test_data/flutter_runner_tests";
}

}  // namespace

// In the checked-in build of flutter runner, the symbol SessionConnection::SessionConnection is
// inlined and the abstract origin crosses compilation unit boundaries. This is less common and
// forces the indexer into a slower mode. Validate that we can find the symbol.
TEST(Index, CrossUnitInline) {
  TestSymbolModule setup(GetFlutterRunnerPath(), "test");
  Err err = setup.Init();
  ASSERT_TRUE(err.ok()) << err.msg();

  Identifier session_connection_ident =
      TestSymbolModule::SplitName("flutter_runner::SessionConnection::SessionConnection");

  std::vector<IndexNode::SymbolRef> refs =
      setup.symbols()->GetIndex().FindExact(session_connection_ident);
  EXPECT_EQ(1u, refs.size());

  // The resolved symbol should be a function.
  LazySymbol lazy = setup.symbols()->symbol_factory()->MakeLazy(refs[0].offset());
  const Symbol* symbol = lazy.Get();
  const Function* function = symbol->As<Function>();
  ASSERT_TRUE(function);

  // Validate name and code ranges.
  EXPECT_EQ("flutter_runner::SessionConnection::SessionConnection", function->GetFullName());
  EXPECT_EQ(AddressRanges(AddressRanges::kCanonical,
                          {AddressRange(0x33d894, 0x33e948), AddressRange(0x33ee14, 0x33ef38),
                           AddressRange(0x33ef74, 0x33ef94)}),
            function->code_ranges());
}

}  // namespace zxdb
