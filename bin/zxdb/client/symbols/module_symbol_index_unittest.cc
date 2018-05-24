// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ostream>

#include "garnet/bin/zxdb/client/symbols/module_symbol_index.h"
#include "garnet/bin/zxdb/client/symbols/test_symbol_module.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(ModuleSymbolIndex, FindFunctionExact) {
  TestSymbolModule module;
  std::string err;
  ASSERT_TRUE(module.Load(&err)) << err;

  ModuleSymbolIndex index;
  index.CreateIndex(module.context(), module.compile_units());

#if 0
  // Enable to dump the found index for debugging purposes.
  std::cout << "Index dump:\n";
  index.root().Dump(std::cout, 1);
#endif

  // Standalone function search.
  auto result = index.FindFunctionExact(TestSymbolModule::kMyFunctionName);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";

  // Standalone function inside a namespace.
  result = index.FindFunctionExact(TestSymbolModule::kNamespaceFunctionName);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";

  // Namespace + class member function search.
  result = index.FindFunctionExact(TestSymbolModule::kMyMemberOneName);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";

  // Same but in the 2nd compilation unit (tests unit-relative addressing).
  result = index.FindFunctionExact(TestSymbolModule::kFunctionInTest2Name);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";

  // Namespace + class + struct with static member function search.
  result = index.FindFunctionExact(TestSymbolModule::kMyMemberTwoName);
  EXPECT_EQ(1u, result.size()) << "Symbol not found.";
}

}  // namespace
