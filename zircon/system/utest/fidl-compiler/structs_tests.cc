// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(StructsTests, GoodPrimitiveDefaultValueLiteral) {
  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    int64 field = 20;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(StructsTests, GoodPrimitiveDefaultValueConstReference) {
  TestLibrary library(R"FIDL(
library example;

const int32 A  = 20;

struct MyStruct {
    int64 field = A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(StructsTests, BadMissingDefaultValueReferenceTarget) {
  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    int64 field = A;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
}

TEST(StructsTests, GoodEnumDefaultValueEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };

struct MyStruct {
    MyEnum field = MyEnum.A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(StructsTests, GoodPrimitiveDefaultValueEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };

struct MyStruct {
    int64 field = MyEnum.A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(StructsTests, BadDefaultValueEnumType) {
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
}

TEST(StructsTests, BadDefaultValuePrimitiveInEnum) {
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
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "MyEnum");
}

TEST(StructsTests, GoodEnumDefaultValueBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };

struct MyStruct {
    MyBits field = MyBits.A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(StructsTests, GoodPrimitiveDefaultValueBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };

struct MyStruct {
    int64 field = MyBits.A;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(StructsTests, BadDefaultValueBitsType) {
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
}

TEST(StructsTests, BadDefaultValuePrimitiveInBits) {
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
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "MyBits");
}

// The old-style of enum-referencing should no longer work.
TEST(StructsTests, BadLegacyEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };

struct MyStruct {
    MyEnum field = A;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
}

TEST(StructsTests, BadDefaultValueNullableString) {
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
}

TEST(StructsTests, BadDuplicateMemberName) {
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
}

TEST(StructsTests, MaxInlineSize) {
  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    array<uint8>:65535 arr;
};
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(StructsTests, InlineSizeExceeds64k) {
  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    array<uint8>:65536 arr;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrInlineSizeExceeds64k);
}

}  // namespace
