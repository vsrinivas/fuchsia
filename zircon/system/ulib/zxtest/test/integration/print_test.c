// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include <zxtest/zxtest.h>

// Sanity tests that enforce compile time check for printing primitive types, and preventing
// undefined symbols.

// Test bool.
TEST(CPrintTest, Bool) {
  bool a = false;

  ASSERT_EQ(a, false);
}

// Test all the bit-sized integral types, both signed and unsigned.
TEST(CPrintTest, Uint8) {
  uint8_t a = 0;

  ASSERT_EQ(a, 0u);
}

TEST(CPrintTest, Int8) {
  int8_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, Uint16) {
  uint16_t a = 0;

  ASSERT_EQ(a, 0u);
}

TEST(CPrintTest, Int16) {
  int16_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, Uint32) {
  uint32_t a = 0;

  ASSERT_EQ(a, 0u);
}

TEST(CPrintTest, Int32) {
  int32_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, Uint64) {
  int64_t a = 0;

  ASSERT_EQ(a, 0u);
}

TEST(CPrintTest, Int64) {
  int64_t a = 0;

  ASSERT_EQ(a, 0);
}

// Test all the built-in integral types. Note in particular that char,
// signed char, and unsigned char are three different types.

TEST(CPrintTest, Char) {
  char c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, UnsignedChar) {
  unsigned char c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, SignedChar) {
  signed char c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, Short) {
  short c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, UnsignedShort) {
  unsigned short c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, Int) {
  int c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, UnsignedInt) {
  unsigned int c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, Long) {
  long c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, UnsignedLong) {
  unsigned long c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, LongLong) {
  long long c = 'a';

  ASSERT_EQ(c, 'a');
}

TEST(CPrintTest, UnsignedLongLong) {
  unsigned long long c = 'a';

  ASSERT_EQ(c, 'a');
}

// Print other commonly used typedefs for integral types.

TEST(CPrintTest, OffT) {
  off_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, SizeT) {
  size_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, SSizeT) {
  ssize_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, IntptrT) {
  intptr_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, UintptrT) {
  uintptr_t a = 0;

  ASSERT_EQ(a, 0);
}

TEST(CPrintTest, PtrdiffT) {
  ptrdiff_t a = 0;

  ASSERT_EQ(a, 0);
}

// Test floating point types.

TEST(CPrintTest, Float) {
  float a = 0.0;

  ASSERT_EQ(a, 0.0);
}

TEST(CPrintTest, Double) {
  double a = 0.0;

  ASSERT_EQ(a, 0.0);
}

TEST(CPrintTest, LongDouble) {
  long double a = 0.0;

  ASSERT_EQ(a, 0.0);
}

// For each pointer type, we test non-null pointers for equality, and
// null pointers for nullity. We test both const and non-const pointers.

// Test 'const char*' first, as it is special cased as a C string.

TEST(CPrintTest, Str) {
  const char* a = "MyStr";

  ASSERT_STREQ(a, "MyStr");

  const char* n = NULL;

  ASSERT_NULL(n);
}

// Test other pointer types. In particular, point to char, to a
// builtin type, to a structure, and to void.

TEST(CPrintTest, CharPointer) {
  char c = '\0';
  char* a = &c;

  ASSERT_EQ(a, &c);

  char* n = NULL;

  ASSERT_NULL(n);
}

TEST(CPrintTest, IntPointer) {
  int i = 0;
  int* a = &i;

  ASSERT_EQ(a, &i);

  int* n = NULL;

  ASSERT_NULL(n);
}

TEST(CPrintTest, ConstIntPointer) {
  int i = 0;
  const int* a = &i;

  ASSERT_EQ(a, &i);

  const int* n = NULL;

  ASSERT_NULL(n);
}

TEST(CPrintTest, StructPointer) {
  struct S {
    int x;
  } s;
  struct S* a = &s;

  ASSERT_EQ(a, &s);

  struct S* n = NULL;

  ASSERT_NULL(n);
}

TEST(CPrintTest, ConstStructPointer) {
  struct S {
    int x;
  } s;
  const struct S* a = &s;

  ASSERT_EQ(a, &s);

  const struct S* n = NULL;

  ASSERT_NULL(n);
}

TEST(CPrintTest, VoidPointer) {
  int i = 0;
  void* a = &i;

  ASSERT_EQ(a, &i);

  void* n = NULL;

  ASSERT_NULL(n);
}

TEST(CPrintTest, ConstVoidPointer) {
  int i = 0;
  const void* a = &i;

  ASSERT_EQ(a, &i);

  const void* n = NULL;

  ASSERT_NULL(n);
}
