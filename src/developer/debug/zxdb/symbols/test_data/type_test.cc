// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// clang-format off
// This file is used to check symbol information so should not be modified by the formatter.

#include <stddef.h>

#include "src/developer/debug/zxdb/symbols/test_data/zxdb_symbol_test.h"

// This file is compiled into a library and used in the DWARFSymboLFactory
// tests to query symbol information. The actual code is not run.

EXPORT const int* GetIntPtr() { return nullptr; }  // Line 15.

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
  const void* v;

  static constexpr int kConstInt = -2;

  // This is a "long double" which forces x86 to use 80-bits and a different encoding.
  static constexpr long double kConstLongDouble = 3.14;

  int MyFunc(char p) { return 1; }
};

EXPORT Struct GetStruct() { return Struct(); }
using StructMemberPtr = int (Struct::*)(char);
StructMemberPtr GetStructMemberPtr() { return &Struct::MyFunc; }

EXPORT void PassRValueRef(int&& rval_ref) {}

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

__attribute__((always_inline)) int InlinedFunction(int param) {
  return param * 2;
}

struct TypeForUsing {
  int a;
};

}  // namespace my_ns

void My2DArray() {
  int array[3][4];
  array[1][2] = 1;
  (void)array;
}

struct ForInline {
  int struct_val = 5;

  __attribute__((always_inline)) int InlinedFunction(int param) {
    return param * struct_val;
  }
};

template<typename T, int i>
struct MyTemplate {
  T t;
  int int_value = i;
};

EXPORT MyTemplate<my_ns::Struct, 42> GetTemplate() {
  MyTemplate<my_ns::Struct, 42> mt;
  mt.t.member_a = 1;
  return mt;
}

// Call both inline member functions and an inlined function declared in
// the same file.
//
// As of this writing, Clang will generate the inline member as an inlined
// subroutine with an abstract origin of the implementation, that in turn
// references the declaration inside the class. The enclosing scope should come
// from the declaration.
//
// The non-member inlined function will skip the declaration, meaning the
// enclosing scope should come from the abstract origin instead.
EXPORT int CallInlineMember(int param) {
  ForInline for_inline;
  return for_inline.InlinedFunction(param + 1);
}
EXPORT int CallInline(int param) { return my_ns::InlinedFunction(param + 1); }

struct StructWithEnums {
  // "Regular" enum but with no values.
  enum RegularEnum {} regular;

  // Amonymous enum (should be forced to be signed).
  enum { ANON_A = -1, ANON_B = 1 } anon;

  // Typed enum class.
  enum class TypedEnum : signed char { TYPED_A = -1, TYPED_B = 1 } typed;
};
StructWithEnums GetStructWithEnums() { return StructWithEnums(); }

EXPORT nullptr_t GetNullPtrT() { return nullptr; }

// TODO(brettw) "TypeForUsing" lacks a test because the function actually
// returns "my_ns::TypeForUsing" so we need to find another way to get the
// using definition for testing.
using my_ns::TypeForUsing;
EXPORT TypeForUsing GetUsing() {
  TypeForUsing result;
  result.a = 92;
  return result;
}

class VirtualBase {
 virtual void DoIt() {}
};

class VirtualDerived : public virtual VirtualBase {
 virtual void DoIt() {}
};

EXPORT VirtualDerived GetVirtualDerived() {
  return VirtualDerived();
}

// TODO(brettw) test:
//   stuff in an anonymous namespace
//   local types defined in functions
