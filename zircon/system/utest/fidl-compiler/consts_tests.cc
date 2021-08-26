// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "test_library.h"

namespace {

template <class PrimitiveType>
void CheckConstEq(TestLibrary& library, const std::string& name, PrimitiveType expected_value,
                  fidl::flat::Constant::Kind expected_constant_kind,
                  fidl::flat::ConstantValue::Kind expected_constant_value_kind) {
  auto const_decl = library.LookupConstant(name);
  ASSERT_NOT_NULL(const_decl);
  ASSERT_EQ(expected_constant_kind, const_decl->value->kind);
  ASSERT_EQ(expected_constant_value_kind, const_decl->value->Value().kind);
  auto numeric_const_value = static_cast<const fidl::flat::NumericConstantValue<PrimitiveType>&>(
      const_decl->value->Value());
  EXPECT_EQ(expected_value, static_cast<PrimitiveType>(numeric_const_value));
}

TEST(ConstsTests, GoodLiteralsTest) {
  TestLibrary library(R"FIDL(
library example;

const uint32 C_SIMPLE   = 11259375;
const uint32 C_HEX_S    = 0xABCDEF;
const uint32 C_HEX_L    = 0XABCDEF;
const uint32 C_BINARY_S = 0b101010111100110111101111;
const uint32 C_BINARY_L = 0B101010111100110111101111;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto check_const_eq = [](TestLibrary& library, const std::string& name, uint32_t expected_value) {
    CheckConstEq<uint32_t>(library, name, expected_value, fidl::flat::Constant::Kind::kLiteral,
                           fidl::flat::ConstantValue::Kind::kUint32);
  };

  check_const_eq(library, "C_SIMPLE", 11259375);
  check_const_eq(library, "C_HEX_S", 11259375);
  check_const_eq(library, "C_HEX_L", 11259375);
  check_const_eq(library, "C_BINARY_S", 11259375);
  check_const_eq(library, "C_BINARY_L", 11259375);
}

TEST(ConstsTests, GoodConstTestBool) {
  TestLibrary library(R"FIDL(
library example;

const bool c = false;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, BadConstTestBoolWithString) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c bool = "foo";
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "\"foo\"");
}

TEST(ConstsTests, BadConstTestBoolWithNumeric) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c bool = 6;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "6");
}

TEST(ConstsTests, GoodConstTestInt32) {
  TestLibrary library(R"FIDL(
library example;

const int32 c = 42;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodConstTestInt32FromOtherConst) {
  TestLibrary library(R"FIDL(
library example;

const int32 b = 42;
const int32 c = b;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, BadConstTestInt32WithString) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c int32 = "foo";
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "\"foo\"");
}

TEST(ConstsTests, BadConstTestInt32WithBool) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c int32 = true;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "true");
}

TEST(ConstsTests, GoodConstTesUint64) {
  TestLibrary library(R"FIDL(
library example;

const int64 a = 42;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodConstTestUint64FromOtherUint32) {
  TestLibrary library(R"FIDL(
library example;

const uint32 a = 42;
const uint64 b = a;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, BadConstTestUint64Negative) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const a uint64 = -42;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-42");
}

TEST(ConstsTests, BadConstTestUint64Overflow) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const a uint64 = 18446744073709551616;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "18446744073709551616");
}

TEST(ConstsTests, GoodConstTestFloat32) {
  TestLibrary library(R"FIDL(
library example;

const float32 b = 1.61803;
const float32 c = -36.46216;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodConstTestFloat32HighLimit) {
  TestLibrary library(R"FIDL(
library example;

const float32 hi = 3.402823e38;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodConstTestFloat32LowLimit) {
  TestLibrary library(R"FIDL(
library example;

const float32 lo = -3.40282e38;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, BadConstTestFloat32HighLimit) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const hi float32 = 3.41e38;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "3.41e38");
}

TEST(ConstsTests, BadConstTestFloat32LowLimit) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const b float32 = -3.41e38;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-3.41e38");
}

TEST(ConstsTests, GoodConstTestString) {
  TestLibrary library(R"FIDL(
library example;

const string:4 c = "four";
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodConstTestStringFromOtherConst) {
  TestLibrary library(R"FIDL(
library example;

const string:4 c = "four";
const string:5 d = c;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

// TODO(fxbug.dev/37314): Both declarations should have the same type.
TEST(ConstsTests, GoodConstTestStringShouldHaveInferredBounds) {
  TestLibrary library(R"FIDL(
library example;

const string INFERRED = "four";
const string:4 EXPLICIT = "four";

)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto inferred_const = library.LookupConstant("INFERRED");
  auto inferred_const_type = fidl::flat::GetType(inferred_const->type_ctor);
  ASSERT_NOT_NULL(inferred_const_type);
  ASSERT_EQ(inferred_const_type->kind, fidl::flat::Type::Kind::kString);
  auto inferred_string_type = static_cast<const fidl::flat::StringType*>(inferred_const_type);
  ASSERT_NOT_NULL(inferred_string_type->max_size);
  ASSERT_EQ(static_cast<uint32_t>(*inferred_string_type->max_size), 4294967295u);

  auto explicit_const = library.LookupConstant("EXPLICIT");
  auto explicit_const_type = fidl::flat::GetType(explicit_const->type_ctor);
  ASSERT_NOT_NULL(explicit_const_type);
  ASSERT_EQ(explicit_const_type->kind, fidl::flat::Type::Kind::kString);
  auto explicit_string_type = static_cast<const fidl::flat::StringType*>(explicit_const_type);
  ASSERT_NOT_NULL(explicit_string_type->max_size);
  ASSERT_EQ(static_cast<uint32_t>(*explicit_string_type->max_size), 4u);
}

TEST(ConstsTests, BadConstTestStringWithNumeric) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c string = 4;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "4");
}

TEST(ConstsTests, BadConstTestStringWithBool) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c string = true;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "true");
}

TEST(ConstsTests, BadConstTestStringWithStringTooLong) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c string:4 = "hello";
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrStringConstantExceedsSizeBound,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "\"hello\"");
}

TEST(ConstsTests, GoodConstTestUsing) {
  TestLibrary library(R"FIDL(
library example;

alias foo = int32;
const foo c = 2;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, BadConstTestUsingWithInconvertibleValue) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

alias foo = int32;
const c foo = "nope";
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "\"nope\"");
}

TEST(ConstsTests, BadConstTestNullableString) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c string:optional = "";
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "string?");
}

TEST(ConstsTests, BadConstTestArray) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c array<int32,2> = -1;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidConstantType);
  // TODO(fxdev.bug/73879): Update string matched when error output respects new
  //  syntax.
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "array<int32>:2");
}

TEST(ConstsTests, BadConstTestVector) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const c vector<int32>:2 = -1;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "vector<int32>:2");
}

TEST(ConstsTests, BadConstTestHandleOfThread) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type obj_type = enum : uint32 {
    NONE = 0;
    THREAD = 2;
};

resource_definition handle : uint32 {
    properties {
        subtype obj_type;
    };
};

const c handle:THREAD = -1;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidConstantType);
  // TODO(fxdev.bug/73879): Update string matched when error output respects new
  //  syntax.
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "handle<thread>");
}

TEST(ConstsTests, GoodConstEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };
const int32 c = MyEnum.A;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodConstBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };
const uint32 c = MyBits.A;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodEnumTypedConstEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };
const MyEnum c = MyEnum.A;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodEnumTypedConstBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };
const MyBits c = MyBits.A;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, BadConstDifferentEnumMemberReference) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyEnum = enum : int32 { VALUE = 1; };
type OtherEnum = enum : int32 { VALUE = 5; };
const c MyEnum = OtherEnum.VALUE;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment,
                                      fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, BadConstDifferentBitsMemberReference) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyBits = bits : uint32 { VALUE = 0x00000001; };
type OtherBits = bits : uint32 { VALUE = 0x00000004; };
const c MyBits = OtherBits.VALUE;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment,
                                      fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, BadConstAssignPrimitiveToEnum) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyEnum = enum : int32 { VALUE = 1; };
const c MyEnum = 5;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyEnum");
}

TEST(ConstsTests, BadConstAssignPrimitiveToBits) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type MyBits = bits : uint32 { VALUE = 0x00000001; };
const c MyBits = 5;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantCannotBeInterpretedAsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyBits");
}

TEST(ConstsTests, GoodMaxBoundTest) {
  TestLibrary library(R"FIDL(
library example;

const string:MAX S = "";

struct Example {
    string:MAX s;
    vector<bool>:MAX v;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodMaxBoundTestConvertToUnbounded) {
  TestLibrary library(R"FIDL(
library example;

const string:MAX A = "foo";
const string B = A;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, GoodMaxBoundTestConvertFromUnbounded) {
  TestLibrary library(R"FIDL(
library example;

const string A = "foo";
const string:MAX B = A;
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, BadMaxBoundTestAssignToConst) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const FOO uint32 = MAX;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, BadMaxBoundTestLibraryQualified) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependency.fidl", R"FIDL(
library dependency;

struct Example {};
)FIDL",
                         &shared);
  TestLibrary converted_dependency;
  ASSERT_COMPILED_AND_CONVERT_INTO(dependency, converted_dependency);

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependency;

type Example = struct { s string:dependency.MAX; };
)FIDL",
                      &shared, experimental_flags);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(converted_dependency)));
  // NOTE(fxbug.dev/72924): we provide a more general error because there are multiple
  // possible interpretations.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(ConstsTests, BadMaxBoundTestLibraryQualifiedWithOldDep) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependency.fidl", R"FIDL(
library dependency;

struct Example {};
)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library("example.fidl", R"FIDL(
library example;

using dependency;

type Example = struct { s string:dependency.MAX; };
)FIDL",
                      &shared, experimental_flags);
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  // NOTE(fxbug.dev/72924): we provide a more general error because there are multiple
  // possible interpretations.
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedConstraint);
}

TEST(ConstsTests, BadParameterizePrimitive) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const u uint8<string> = 0;
)FIDL",
                      experimental_flags);
  // NOTE(fxbug.dev/72924): we provide a more general error in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ConstsTests, BadConstTestAssignTypeName) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  for (auto type_declaration : {
           "type Example = struct {};",
           "type Example = table {};",
           "service Example {};",
           "protocol Example {};",
           "type Example = bits { A = 1; };",
           "type Example = enum { A = 1; };",
           "type Example = union { 1: A bool; };",
           "alias Example = string;",
       }) {
    std::ostringstream ss;
    ss << "library example;\n";
    ss << type_declaration << "\n";
    ss << "const FOO uint32 = Example;\n";

    TestLibrary library(ss.str(), experimental_flags);
    ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrExpectedValueButGotType,
                                        fidl::ErrCannotResolveConstantValue);
  }
}

TEST(ConstsTests, BadNameCollision) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

const FOO uint8 = 0;
const FOO uint8 = 1;
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(ConstsTests, GoodMultiFileConstReference) {
  TestLibrary library("first.fidl", R"FIDL(
library example;

struct Protein {
    vector<uint64>:SMALL_SIZE amino_acids;
};
)FIDL");

  library.AddSource("second.fidl", R"FIDL(
library example;

const uint32 SMALL_SIZE = 4;
)FIDL");

  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ConstsTests, BadUnknownEnumMemberTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type EnumType = enum : int32 {
    A = 0x00000001;
    B = 0x80;
    C = 0x2;
};

const dee EnumType = EnumType.D;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnknownEnumMember,
                                      fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, BadUnknownBitsMemberTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type BitsType = bits {
    A = 2;
    B = 4;
    C = 8;
};

const dee BitsType = BitsType.D;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnknownBitsMember,
                                      fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, GoodOrOperatorTest) {
  fidl::ExperimentalFlags experimental_flags;

  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint8 {
  A = 0x00000001;
  B = 0x00000002;
  C = 0x00000004;
  D = 0x00000008;
};
const MyBits bitsValue = MyBits.A | MyBits.B | MyBits.D;
const uint16 Result = MyBits.A | MyBits.B | MyBits.D;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED_AND_CONVERT(library);

  CheckConstEq<uint16_t>(library, "Result", 11, fidl::flat::Constant::Kind::kBinaryOperator,
                         fidl::flat::ConstantValue::Kind::kUint16);
}

TEST(ConstsTests, BadOrOperatorDifferentTypesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

const one uint8 = 0x0001;
const two_fifty_six uint16 = 0x0100;
const two_fifty_seven uint8 = one | two_fifty_six;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrCannotConvertConstantToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "uint8");
}

TEST(ConstsTests, GoodOrOperatorDifferentTypesTest) {
  fidl::ExperimentalFlags experimental_flags;

  TestLibrary library(R"FIDL(
library example;

const uint8 one = 0x0001;
const uint16 two_fifty_six = 0x0100;
const uint16 two_fifty_seven = one | two_fifty_six;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED_AND_CONVERT(library);

  CheckConstEq<uint16_t>(library, "two_fifty_seven", 257,
                         fidl::flat::Constant::Kind::kBinaryOperator,
                         fidl::flat::ConstantValue::Kind::kUint16);
}

TEST(ConstsTests, BadOrOperatorNonPrimitiveTypesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

const HI string = "hi";
const THERE string = "there";
const result string = HI | THERE;
  )FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrOrOperatorOnNonPrimitiveValue,
                                      fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, GoodOrOperatorParenthesesTest) {
  fidl::ExperimentalFlags experimental_flags;

  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint8 {
  A = 0x00000001;
  B = 0x00000002;
  C = 0x00000004;
  D = 0x00000008;
};
const MyBits three = MyBits.A | MyBits.B;
const MyBits seven = three | MyBits.C;
const MyBits fifteen = ( three | seven ) | MyBits.D;
const MyBits bitsValue = MyBits.A | ( ( ( MyBits.A | MyBits.B ) | MyBits.D ) | MyBits.C );
)FIDL",
                      std::move(experimental_flags));
  ASSERT_COMPILED_AND_CONVERT(library);

  CheckConstEq<uint8_t>(library, "three", 3, fidl::flat::Constant::Kind::kBinaryOperator,
                        fidl::flat::ConstantValue::Kind::kUint8);
  CheckConstEq<uint8_t>(library, "seven", 7, fidl::flat::Constant::Kind::kBinaryOperator,
                        fidl::flat::ConstantValue::Kind::kUint8);
  CheckConstEq<uint8_t>(library, "fifteen", 15, fidl::flat::Constant::Kind::kBinaryOperator,
                        fidl::flat::ConstantValue::Kind::kUint8);
  CheckConstEq<uint8_t>(library, "bitsValue", 15, fidl::flat::Constant::Kind::kBinaryOperator,
                        fidl::flat::ConstantValue::Kind::kUint8);
}

TEST(ConstsTests, BadOrOperatorMissingRightParenTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library = TestLibrary(R"FIDL(
library example;

const three uint16 = 3;
const seven uint16 = 7;
const eight uint16 = 8;
const fifteen uint16 = ( three | seven | eight;
)FIDL",
                                    std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(ConstsTests, BadOrOperatorMissingLeftParenTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library = TestLibrary(R"FIDL(
library example;

const three uint16 = 3;
const seven uint16 = 7;
const eight uint16 = 8;
const fifteen uint16 = three | seven | eight );
)FIDL",
                                    std::move(experimental_flags));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrExpectedDeclaration);
}

TEST(ConstsTests, BadOrOperatorMisplacedParenTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library = TestLibrary(R"FIDL(
library example;

const three uint16 = 3;
const seven uint16 = 7;
const eight uint16 = 8;
const fifteen uint16 = ( three | seven | ) eight;
)FIDL",
                                    std::move(experimental_flags));
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedToken);
}

TEST(ConstsTests, BadIdentifierConstMismatchedTypesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type OneEnum = enum {
    A = 1;
};
type AnotherEnum = enum {
    B = 1;
};
const a OneEnum = OneEnum.A;
const b AnotherEnum = a;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "AnotherEnum");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "OneEnum");
}

TEST(ConstsTests, BadEnumBitsConstMismatchedTypesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);

  TestLibrary library(R"FIDL(
library example;

type OneEnum = enum {
    A = 1;
};
type AnotherEnum = enum {
    B = 1;
};
const a OneEnum = AnotherEnum.B;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "AnotherEnum");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "OneEnum");
}

}  // namespace
