// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment);
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType);
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment);
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType);
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidStructMemberType);
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrDuplicateStructMemberName);
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInlineSizeExceeds64k);
}

TEST(StructsTests, BadMutuallyRecursive) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Yin = struct {
  yang Yang;
};

type Yang = struct {
  yin Yin;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrIncludeCycle);
}

TEST(StructsTests, BadBoxCannotBeNullable) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type BoxedStruct = struct {};

type Foo = struct {
  foo box<Foo>:optional;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrBoxCannotBeNullable);
}

TEST(StructsTests, BadBoxedTypeCannotBeNullable) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type BoxedStruct = struct {};

type Foo = struct {
  foo box<Foo:optional>;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrBoxedTypeCannotBeNullable);
}

TEST(StructsTests, BadTypeCannotBeBoxed) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  for (const std::string& definition : {
           "type Foo = struct { union_member box<union { 1: data uint8; }>; };",
           "type Foo = struct { table_member box<table { 1: data uint8; }>; };",
           "type Foo = struct { enum_member box<enum { DATA = 1; }>; };",
           "type Foo = struct { bits_member box<bits { DATA = 1; }>; };",
           "type Foo = struct { array_member box<array<uint8, 1>>; };",
           "type Foo = struct { vector_member box<vector<uint8>>; };",
           "type Foo = struct { string_member box<string>; };",
           "type Foo = struct { prim_member box<int32>; };",
           "type Foo = struct { resource_member box<zx.handle>; };",
       }) {
    std::string fidl_library = "library example;\nusing zx;\n\n" + definition + "\n";
    auto library = WithLibraryZx(fidl_library, experimental_flags);
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeBoxed);
  }
}

}  // namespace
