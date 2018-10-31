// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/symbols/test_data/zxdb_symbol_test.h"

// This file is compiled into a library and used in the DWARFSymboLFactory
// tests to query symbol information. The actual code is not run.

EXPORT const int* GetIntPtr() { return nullptr; }  // Line 10.

EXPORT char GetString() {
  const char str_array[14] = "Hello, world.";
  return str_array[0];
}

namespace my_ns {

struct Base1 {
  int base1;
};
struct Base2 {
  int base2;
};

struct Struct : public Base1, private Base2 {
  int member_a;
  Struct* member_b;
};

EXPORT Struct GetStruct() { return Struct(); }

EXPORT void PassRValueRef(int&& rval_ref) { }

// This provides a test for struct type decode, function parameters, and
// local variables.
EXPORT int DoStructCall(const Struct& arg1, int arg2) {
  // This uses "volatile" to prevent the values from being optimized out.
  volatile int var1 = 2;
  var1 *= 2;

  // Introduce a lexical scope with another varuable in it.
  {
    volatile Struct var2;
    var2.member_a = 1;
    return var1 + var2.member_a;
  }
}

}  // namespace my_ns

void My2DArray() {
  int array[3][4];
  array[1][2] = 1;
  (void)array;
}

// TODO(brettw) test:
//   stuff in an anonymous namespace
//   typedef
//   using
//   local types defined in functions
