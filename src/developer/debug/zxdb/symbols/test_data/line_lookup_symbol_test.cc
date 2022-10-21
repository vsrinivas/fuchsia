// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/test_data/zxdb_symbol_test.h"

// This code isn't executed but is used for ModuleSymbolsImpl unit tests.
// clang-format off
// IMPORTANT: Do not change the layout of this file! They tests depend on
// absolute line indices into this file.

namespace {

template <int v>
NOINLINE int LineLookupTest(int b) {  // Line 15: function begin.
  if (v == 0) {
    return b * 2;  // Line 17, only present in one template instantiation.
  } else {
    return b * 3;
  }
}

}  // namespace

EXPORT int DoLineLookupTest(int i) {
  // Line 26: Comment line.
  int result = LineLookupTest<0>(i);  // Line 27.
  result += LineLookupTest<1>(i);     // Line 28.
  return result;
}

namespace {

__attribute__((always_inline)) int InlineCall(int i) {  // Line 34.
  return LineLookupTest<0>(i + 2);                      // Line 35.
}

}  // namespace

// See ModuleSymbols.ResolveLineInputLocation_Inlines test.
EXPORT int DoInlineLineLookupTest(int i) {
  int result = InlineCall(i + 1);  // Line 42.
  result *= 2;
  return result;
}
