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
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(StructsTests, GoodPrimitiveDefaultValueConstReference) {
  TestLibrary library(R"FIDL(
library example;

const int32 A  = 20;

struct MyStruct {
    int64 field = A;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(StructsTests, BadMissingDefaultValueReferenceTarget) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct {
    field int64 = A;
};
)FIDL",
                      experimental_flags);
  ASSERT_FALSE(library.Compile());
}

TEST(StructsTests, BadMissingDefaultValueReferenceTargetOld) {
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
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(StructsTests, GoodPrimitiveDefaultValueEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };

struct MyStruct {
    int64 field = MyEnum.A;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(StructsTests, BadDefaultValueEnumType) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyEnum = enum : int32 { A = 1; };
type OtherEnum = enum : int32 { A = 1; };

type MyStruct = struct {
    field MyEnum = OtherEnum.A;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED(library, fidl::ErrMismatchedNameTypeAssignment);
}

TEST(StructsTests, BadDefaultValueEnumTypeOld) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 1; };
enum OtherEnum : int32 { A = 1; };

struct MyStruct {
    MyEnum field = OtherEnum.A;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrMismatchedNameTypeAssignment);
}

TEST(StructsTests, BadDefaultValuePrimitiveInEnum) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyEnum = enum : int32 { A = 1; };

type MyStruct = struct {
    field MyEnum = 1;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED(library, fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyEnum");
}

TEST(StructsTests, BadDefaultValuePrimitiveInEnumOld) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 1; };

struct MyStruct {
    MyEnum field = 1;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyEnum");
}

TEST(StructsTests, GoodEnumDefaultValueBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };

struct MyStruct {
    MyBits field = MyBits.A;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(StructsTests, GoodPrimitiveDefaultValueBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };

struct MyStruct {
    int64 field = MyBits.A;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(StructsTests, BadDefaultValueBitsType) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyBits = bits : uint32 { A = 0x00000001; };
type OtherBits = bits : uint32 { A = 0x00000001; };

type MyStruct = struct {
    field MyBits = OtherBits.A;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED(library, fidl::ErrMismatchedNameTypeAssignment);
}

TEST(StructsTests, BadDefaultValueBitsTypeOld) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };
bits OtherBits : uint32 { A = 0x00000001; };

struct MyStruct {
    MyBits field = OtherBits.A;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrMismatchedNameTypeAssignment);
}

TEST(StructsTests, BadDefaultValuePrimitiveInBits) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyBits = enum : int32 { A = 0x00000001; };

type MyStruct = struct {
    field MyBits = 1;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED(library, fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyBits");
}

TEST(StructsTests, BadDefaultValuePrimitiveInBitsOld) {
  TestLibrary library(R"FIDL(
library example;

enum MyBits : int32 { A = 0x00000001; };

struct MyStruct {
    MyBits field = 1;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyBits");
}

// The old-style of enum-referencing should no longer work.
TEST(StructsTests, BadLegacyEnumMemberReference) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyEnum = enum : int32 { A = 5; };

type MyStruct = struct {
    field MyEnum = A;
};
)FIDL",
                      experimental_flags);
  ASSERT_FALSE(library.Compile());
}

TEST(StructsTests, BadLegacyEnumMemberReferenceOld) {
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
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct {
    field string:optional = "";
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED(library, fidl::ErrInvalidStructMemberType);
}

TEST(StructsTests, BadDefaultValueNullableStringOld) {
  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    string? field = "";
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrInvalidStructMemberType);
}

TEST(StructsTests, BadDuplicateMemberName) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Duplicates = struct {
    s string;
    s uint8;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED(library, fidl::ErrDuplicateStructMemberName);
}

TEST(StructsTests, BadDuplicateMemberNameOld) {
  TestLibrary library(R"FIDL(
library example;

struct Duplicates {
    string s;
    uint8 s;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrDuplicateStructMemberName);
}

TEST(StructsTests, GoodMaxInlineSize) {
  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    array<uint8>:65535 arr;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(StructsTests, BadInlineSizeExceeds64k) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyStruct = struct {
    arr array<uint8,65536>;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED(library, fidl::ErrInlineSizeExceeds64k);
}

TEST(StructsTests, BadInlineSizeExceeds64kOld) {
  TestLibrary library(R"FIDL(
library example;

struct MyStruct {
    array<uint8>:65536 arr;
};
)FIDL");
  ASSERT_ERRORED(library, fidl::ErrInlineSizeExceeds64k);
}

}  // namespace
