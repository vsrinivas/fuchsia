// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

bool GoodPrimitiveDefaultValueLiteral() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    int64 field = 20;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodPrimitiveDefaultValueConstReference() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

const int32 A  = 20;

struct MyStruct {
    int64 field = A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool BadMissingDefaultValueReferenceTarget() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    int64 field = A;
};
)FIDL");
  ASSERT_FALSE(library.Compile());

  END_TEST;
}

bool GoodEnumDefaultValueEnumMemberReference() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };

struct MyStruct {
    MyEnum field = MyEnum.A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodPrimitiveDefaultValueEnumMemberReference() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };

struct MyStruct {
    int64 field = MyEnum.A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool BadDefaultValueEnumType() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 1; };
enum OtherEnum : int32 { A = 1; };

struct MyStruct {
    MyEnum field = OtherEnum.A;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMismatchedNameTypeAssignment);

  END_TEST;
}

bool BadDefaultValuePrimitiveInEnum() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 1; };

struct MyStruct {
    MyEnum field = 1;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "MyEnum");

  END_TEST;
}

bool GoodEnumDefaultValueBitsMemberReference() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };

struct MyStruct {
    MyBits field = MyBits.A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool GoodPrimitiveDefaultValueBitsMemberReference() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };

struct MyStruct {
    int64 field = MyBits.A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool BadDefaultValueBitsType() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };
bits OtherBits : uint32 { A = 0x00000001; };

struct MyStruct {
    MyBits field = OtherBits.A;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMismatchedNameTypeAssignment);

  END_TEST;
}

bool BadDefaultValuePrimitiveInBits() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum MyBits : int32 { A = 0x00000001; };

struct MyStruct {
    MyBits field = 1;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_STR_STR(errors[0]->msg.c_str(), "MyBits");

  END_TEST;
}

// The old-style of enum-referencing should no longer work.
bool BadLegacyEnumMemberReference() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };

struct MyStruct {
    MyEnum field = A;
};
)FIDL");
  ASSERT_FALSE(library.Compile());

  END_TEST;
}

bool BadDefaultValueNullableString() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    string? field = "";
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrInvalidStructMemberType);

  END_TEST;
}

bool BadDuplicateMemberName() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct Duplicates {
    string s;
    uint8 s;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateStructMemberName);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(structs_tests)

RUN_TEST(GoodPrimitiveDefaultValueLiteral)
RUN_TEST(GoodPrimitiveDefaultValueConstReference)
RUN_TEST(BadMissingDefaultValueReferenceTarget)

RUN_TEST(GoodEnumDefaultValueEnumMemberReference)
RUN_TEST(GoodPrimitiveDefaultValueEnumMemberReference)
RUN_TEST(BadDefaultValueEnumType)
RUN_TEST(BadDefaultValuePrimitiveInEnum)

RUN_TEST(GoodEnumDefaultValueBitsMemberReference)
RUN_TEST(GoodPrimitiveDefaultValueBitsMemberReference)
RUN_TEST(BadDefaultValueBitsType)
RUN_TEST(BadDefaultValuePrimitiveInBits)
RUN_TEST(BadLegacyEnumMemberReference)
RUN_TEST(BadDefaultValueNullableString)
RUN_TEST(BadDuplicateMemberName)

END_TEST_CASE(structs_tests)
