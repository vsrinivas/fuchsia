// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "test_library.h"

namespace {

TEST(RecoverableCompilationTests, BadRecoverInLibraryConsume) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
protocol P {};              // Error: name collision

type foo = struct {};
type Foo = struct {};       // Error: canonical name collision
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  EXPECT_ERR(errors[0], fidl::ErrNameCollision);
  EXPECT_ERR(errors[1], fidl::ErrNameCollisionCanonical);
}

TEST(RecoverableCompilationTests, BadRecoverInLibraryCompile) {
  TestLibrary library(R"FIDL(
library example;

type Union = union {
    1: string_value string;
    2: unknown_value UnknownType; // Error: unknown type
};

type Enum = enum {
    ZERO = 0;
    ONE = 1;
    TWO = 1;                      // Error: duplicate value
    THREE = 3;
};

type OtherEnum = enum {
    NONE = 0;
    ONE = 1;
    ONE = 2;                      // Error: duplicate name
};

type NonDenseTable = table {
    1: s string;
    3: b uint8;                   // Error: non-dense ordinals
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 4);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberValue);
  ASSERT_ERR(errors[1], fidl::ErrNonDenseOrdinal);
  ASSERT_ERR(errors[2], fidl::ErrDuplicateMemberName);
  ASSERT_ERR(errors[3], fidl::ErrUnknownType);
}

TEST(RecoverableCompilationTests, BadRecoverInLibraryVerifyAttributePlacement) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@transitional            // Error: invalid placement
type Table = table {
    1: foo string;
};

@max_bytes("1")          // Error: too large
type Struct = struct {
    foo uint16;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrTooManyBytes);
  ASSERT_ERR(errors[1], fidl::ErrInvalidAttributePlacement);
}

TEST(RecoverableCompilationTests, BadRecoverInAttributeCompile) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo(first="a", first="b")   // Error: duplicate args
@bar(first=3, second=4)      // Error: x2 can only use string or bool
@foo                         // Error: duplicate attribute
type Enum = enum {
    FOO                      // Error: cannot resolve enum member
        = "not a number";    // Error: cannot be interpreted as uint32
};
)FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 6);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateAttributeArg);
  ASSERT_ERR(errors[1], fidl::ErrCanOnlyUseStringOrBool);
  ASSERT_ERR(errors[2], fidl::ErrCanOnlyUseStringOrBool);
  ASSERT_ERR(errors[3], fidl::ErrDuplicateAttribute);
  ASSERT_ERR(errors[4], fidl::ErrTypeCannotBeConvertedToType);
  ASSERT_ERR(errors[5], fidl::ErrCouldNotResolveMember);
}

TEST(RecoverableCompilationTests, BadRecoverInConst) {
  TestLibrary library(R"FIDL(
library example;

@attr(1)
const FOO string = 2;
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  EXPECT_ERR(errors[0], fidl::ErrCanOnlyUseStringOrBool);
  EXPECT_ERR(errors[1], fidl::ErrTypeCannotBeConvertedToType);
  EXPECT_ERR(errors[2], fidl::ErrCannotResolveConstantValue);
}

TEST(RecoverableCompilationTests, BadRecoverInBits) {
  TestLibrary library(R"FIDL(
library example;

type Foo = bits {
    BAR                    // Error: cannot resolve bits member
        = "not a number";  // Error: cannot interpret as uint32
    QUX = nonexistent;     // Error: cannot resolve bits member
    bar = 2;               // Error: canonical name conflicts with 'bar'
    BAZ = 2;               // Error: duplicate value 2
    XYZ = 3;               // Error: not a power of two
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 6);
  EXPECT_ERR(errors[0], fidl::ErrTypeCannotBeConvertedToType);
  EXPECT_ERR(errors[1], fidl::ErrCouldNotResolveMember);
  EXPECT_ERR(errors[2], fidl::ErrCouldNotResolveMember);
  EXPECT_ERR(errors[3], fidl::ErrDuplicateMemberNameCanonical);
  EXPECT_ERR(errors[4], fidl::ErrDuplicateMemberValue);
  EXPECT_ERR(errors[5], fidl::ErrBitsMemberMustBePowerOfTwo);
}

TEST(RecoverableCompilationTests, BadRecoverInEnum) {
  TestLibrary library(R"FIDL(
library example;

type Foo = flexible enum : uint8 {
    BAR                    // Error: cannot resolve enum member
        = "not a number";  // Error: cannot interpret as uint32
    QUX = nonexistent;     // Error: cannot resolve enum member
    bar = 2;               // Error: canonical name conflicts with 'bar'
    BAZ = 2;               // Error: duplicate value 2
    XYZ = 255;             // Error: max value on flexible enum
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 6);
  EXPECT_ERR(errors[0], fidl::ErrTypeCannotBeConvertedToType);
  EXPECT_ERR(errors[1], fidl::ErrCouldNotResolveMember);
  EXPECT_ERR(errors[2], fidl::ErrCouldNotResolveMember);
  EXPECT_ERR(errors[3], fidl::ErrDuplicateMemberNameCanonical);
  EXPECT_ERR(errors[4], fidl::ErrDuplicateMemberValue);
  EXPECT_ERR(errors[5], fidl::ErrFlexibleEnumMemberWithMaxValue);
}

TEST(RecoverableCompilationTests, BadRecoverInStruct) {
  TestLibrary library(R"FIDL(
library example;

type Foo = struct {
    bar string<1>;     // Error: unexpected layout parameter
    qux nonexistent;   // Error: unknown type
    BAR                // Error: canonical name conflicts with 'bar'
        bool           // Error: cannot resolve default value
        = "not bool";  // Error: cannot interpret as bool
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 5);
  EXPECT_ERR(errors[0], fidl::ErrWrongNumberOfLayoutParameters);
  EXPECT_ERR(errors[1], fidl::ErrUnknownType);
  EXPECT_ERR(errors[2], fidl::ErrDuplicateStructMemberNameCanonical);
  EXPECT_ERR(errors[3], fidl::ErrTypeCannotBeConvertedToType);
  EXPECT_ERR(errors[4], fidl::ErrCouldNotResolveMemberDefault);
}

TEST(RecoverableCompilationTests, BadRecoverInTable) {
  TestLibrary library(R"FIDL(
library example;

type Foo = table {
    1: bar string:optional;  // Error: table member cannot be optional
    1: qux                   // Error: duplicate ordinal
       nonexistent;          // Error: unknown type
    // 2: reserved;          // Error: not dense
    3: BAR bool;             // Error: canonical name conflicts with 'bar'
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 5);
  EXPECT_ERR(errors[0], fidl::ErrNullableTableMember);
  EXPECT_ERR(errors[1], fidl::ErrDuplicateTableFieldOrdinal);
  EXPECT_ERR(errors[2], fidl::ErrUnknownType);
  EXPECT_ERR(errors[3], fidl::ErrDuplicateTableFieldNameCanonical);
  EXPECT_ERR(errors[4], fidl::ErrNonDenseOrdinal);
}

TEST(RecoverableCompilationTests, BadRecoverInUnion) {
  TestLibrary library(R"FIDL(
library example;

type Foo = union {
    1: bar string:optional;  // Error: union member cannot be optional
    1: qux                   // Error: duplicate ordinal
        nonexistent;         // Error: unknown type
    // 2: reserved;          // Error: not dense
    3: BAR bool;             // Error: canonical name conflicts with 'bar'
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 5);
  EXPECT_ERR(errors[0], fidl::ErrNullableUnionMember);
  EXPECT_ERR(errors[1], fidl::ErrDuplicateUnionMemberOrdinal);
  EXPECT_ERR(errors[2], fidl::ErrUnknownType);
  EXPECT_ERR(errors[3], fidl::ErrDuplicateUnionMemberNameCanonical);
  EXPECT_ERR(errors[4], fidl::ErrNonDenseOrdinal);
}

TEST(RecoverableCompilationTests, BadRecoverInProtocol) {
  TestLibrary library(R"FIDL(
library example;

protocol Foo {
    compose nonexistent;   // Error: unknown type
    @selector("not good")  // Error: invalid selector
    Bar();
    BAR() -> (struct {     // Error: canonical name conflicts with 'bar'
        b bool:optional;   // Error: bool cannot be optional
    }) error nonexistent;  // Error: unknown type
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 5);
  EXPECT_ERR(errors[0], fidl::ErrUnknownType);
  EXPECT_ERR(errors[1], fidl::ErrInvalidSelectorValue);
  EXPECT_ERR(errors[2], fidl::ErrDuplicateMethodNameCanonical);
  EXPECT_ERR(errors[3], fidl::ErrCannotBeNullable);
  EXPECT_ERR(errors[4], fidl::ErrUnknownType);
}

TEST(RecoverableCompilationTests, BadRecoverInService) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
service Foo {
    bar string;                   // Error: must be client_end
    baz nonexistent;              // Error: unknown type
    qux server_end:P;             // Error: must be client_end
    BAR                           // Error: canonical name conflicts with 'bar'
        client_end:<P,optional>;  // Error: cannot be optional
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 5);
  EXPECT_ERR(errors[0], fidl::ErrOnlyClientEndsInServices);
  EXPECT_ERR(errors[1], fidl::ErrUnknownType);
  EXPECT_ERR(errors[2], fidl::ErrOnlyClientEndsInServices);
  EXPECT_ERR(errors[3], fidl::ErrDuplicateServiceMemberNameCanonical);
  EXPECT_ERR(errors[4], fidl::ErrNullableServiceMember);
}

}  // namespace
