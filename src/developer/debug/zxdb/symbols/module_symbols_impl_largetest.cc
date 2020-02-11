// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"
#include "src/developer/debug/zxdb/symbols/module_symbols_impl.h"
#include "src/developer/debug/zxdb/symbols/test_symbol_module.h"

// This file contains the tests for ModuleSymbolsImpl that use the large optional test data. See
// zxdb/BUILD.gn for more.

namespace zxdb {

namespace {

std::string GetDebugAgentPath() {
  return TestSymbolModule::GetTestDataDir() + "large_test_data/debug_agent";
}

}  // namespace

// The debug_agent is compiled in an optimized code for Arm64. Since ARM has a link register,
// functions don't necessarily have prologues and inlined routines can start right at the beginning
// of a non-inlined routine.

// In this binary, the DIE @ 0x006a811f is "_ZN7cmdline17GeneralArgsParserC2Ev" and it starts
// immediately with a chain of inline functions, all starting at the same address (0x56a60):
//   - "_ZNSt3__212basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEEC2IDnEEPKc"
//   - "_ZNSt3__212basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6__initEPKcm"
//   - "_ZNSt3__211char_traitsIcE4copyEPcPKcm"
TEST(ModuleSymbols, AmbiguousInline) {
  TestSymbolModule setup(GetDebugAgentPath(), "test");
  Err err = setup.Init();
  ASSERT_TRUE(err.ok()) << err.msg();

  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  // This is the ambiguous address: 4 functions, 1 real and 3 inline, all begin here.
  InputLocation input_location(0x56a60);

  ResolveOptions options;
  options.ambiguous_inline = ResolveOptions::AmbiguousInline::kInner;

  auto result = setup.symbols()->ResolveInputLocation(symbol_context, input_location, options);
  ASSERT_EQ(1u, result.size());

  // Most specific function should resolve to the inner inline one.
  const Function* func = result[0].symbol().Get()->AsFunction();
  ASSERT_TRUE(func);
  EXPECT_EQ("std::__2::char_traits<char>::copy", func->GetFullName());

  // Least specific function is the outer physical one.
  options.ambiguous_inline = ResolveOptions::AmbiguousInline::kOuter;
  result = setup.symbols()->ResolveInputLocation(symbol_context, input_location, options);
  ASSERT_EQ(1u, result.size());

  func = result[0].symbol().Get()->AsFunction();
  ASSERT_TRUE(func);
  EXPECT_EQ("cmdline::GeneralArgsParser::GeneralArgsParser", func->GetFullName());
}

}  // namespace zxdb
