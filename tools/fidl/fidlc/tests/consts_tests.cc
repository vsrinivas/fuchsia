// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

template <class PrimitiveType>
void CheckConstEq(TestLibrary& library, std::string_view name, PrimitiveType expected_value,
                  fidl::flat::Constant::Kind expected_constant_kind,
                  fidl::flat::ConstantValue::Kind expected_constant_value_kind) {
  auto const_decl = library.LookupConstant(name);
  ASSERT_NOT_NULL(const_decl);
  ASSERT_EQ(expected_constant_kind, const_decl->value->kind);
  ASSERT_EQ(expected_constant_value_kind, const_decl->value->Value().kind);
  auto& numeric_const_value = static_cast<const fidl::flat::NumericConstantValue<PrimitiveType>&>(
      const_decl->value->Value());
  EXPECT_EQ(expected_value, static_cast<PrimitiveType>(numeric_const_value));
}

TEST(ConstsTests, GoodLiteralsTest) {
  TestLibrary library(R"FIDL(library example;

const C_SIMPLE uint32 = 11259375;
const C_HEX_S uint32 = 0xABCDEF;
const C_HEX_L uint32 = 0XABCDEF;
const C_BINARY_S uint32 = 0b101010111100110111101111;
const C_BINARY_L uint32 = 0B101010111100110111101111;
)FIDL");
  ASSERT_COMPILED(library);

  auto check_const_eq = [](TestLibrary& library, std::string_view name, uint32_t expected_value) {
    CheckConstEq<uint32_t>(library, name, expected_value, fidl::flat::Constant::Kind::kLiteral,
                           fidl::flat::ConstantValue::Kind::kUint32);
  };

  check_const_eq(library, "C_SIMPLE", 11259375);
  check_const_eq(library, "C_HEX_S", 11259375);
  check_const_eq(library, "C_HEX_L", 11259375);
  check_const_eq(library, "C_BINARY_S", 11259375);
  check_const_eq(library, "C_BINARY_L", 11259375);
}

TEST(ConstsTests, GoodHexLiteralTest) {
  TestLibrary library;
  library.AddFile("good/fi-0004.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodOctLiteralTest) {
  TestLibrary library;
  library.AddFile("good/fi-0005.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodConstTestBool) {
  TestLibrary library(R"FIDL(library example;

const c bool = false;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, BadConstTestBoolWithString) {
  TestLibrary library;
  library.AddFile("bad/fi-0065-a.test.fidl");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "\"foo\"");
}

TEST(ConstsTests, BadConstTestBoolWithNumeric) {
  TestLibrary library(R"FIDL(
library example;

const c bool = 6;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "6");
}

TEST(ConstsTests, GoodConstTestInt32) {
  TestLibrary library(R"FIDL(library example;

const c int32 = 42;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodConstTestInt32FromOtherConst) {
  TestLibrary library(R"FIDL(library example;

const b int32 = 42;
const c int32 = b;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, BadConstTestInt32WithString) {
  TestLibrary library(R"FIDL(
library example;

const c int32 = "foo";
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "\"foo\"");
}

TEST(ConstsTests, BadConstTestInt32WithBool) {
  TestLibrary library(R"FIDL(
library example;

const c int32 = true;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "true");
}

TEST(ConstsTests, GoodConstTestInt64) {
  TestLibrary library;
  library.AddFile("good/fi-0066-b.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodConstTesUint64) {
  TestLibrary library;
  library.AddFile("good/fi-0066-a.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodConstTestUint64FromOtherUint32) {
  TestLibrary library(R"FIDL(library example;

const a uint32 = 42;
const b uint64 = a;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, BadConstTestUint64Negative) {
  TestLibrary library;
  library.AddFile("bad/fi-0066.test.fidl");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-42");
}

TEST(ConstsTests, BadConstTestUint64Overflow) {
  TestLibrary library(R"FIDL(
library example;

const a uint64 = 18446744073709551616;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "18446744073709551616");
}

TEST(ConstsTests, GoodConstTestFloat32) {
  TestLibrary library(R"FIDL(library example;

const b float32 = 1.61803;
const c float32 = -36.46216;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodConstTestFloat32HighLimit) {
  TestLibrary library(R"FIDL(library example;

const hi float32 = 3.402823e38;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodConstTestFloat32LowLimit) {
  TestLibrary library(R"FIDL(library example;

const lo float32 = -3.40282e38;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, BadConstTestFloat32HighLimit) {
  TestLibrary library(R"FIDL(
library example;

const hi float32 = 3.41e38;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "3.41e38");
}

TEST(ConstsTests, BadConstTestFloat32LowLimit) {
  TestLibrary library(R"FIDL(
library example;

const b float32 = -3.41e38;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrConstantOverflowsType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "-3.41e38");
}

TEST(ConstsTests, GoodConstTestString) {
  TestLibrary library;
  library.AddFile("good/fi-0002.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodConstTestStringFromOtherConst) {
  TestLibrary library(R"FIDL(library example;

const c string:4 = "four";
const d string:5 = c;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, BadConstTestStringWithNumeric) {
  TestLibrary library(R"FIDL(
library example;

const c string = 4;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "4");
}

TEST(ConstsTests, BadConstTestStringWithBool) {
  TestLibrary library(R"FIDL(
library example;

const c string = true;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "true");
}

TEST(ConstsTests, BadConstTestStringWithStringTooLong) {
  TestLibrary library(R"FIDL(
library example;

const c string:4 = "hello";
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "\"hello\"");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "type string:5");
}

TEST(ConstsTests, GoodConstTestUsing) {
  TestLibrary library(R"FIDL(library example;

alias foo = int32;
const c foo = 2;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, BadConstTestUsingWithInconvertibleValue) {
  TestLibrary library(R"FIDL(
library example;

alias foo = int32;
const c foo = "nope";
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "\"nope\"");
}

TEST(ConstsTests, BadConstTestNullableString) {
  TestLibrary library(R"FIDL(
library example;

const c string:optional = "";
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "string?");
}

TEST(ConstsTests, BadConstTestArray) {
  TestLibrary library(R"FIDL(
library example;

const c array<int32,2> = -1;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "array<int32, 2>");
}

TEST(ConstsTests, BadConstTestVector) {
  TestLibrary library(R"FIDL(
library example;

const c vector<int32>:2 = -1;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "vector<int32>:2");
}

TEST(ConstsTests, BadConstTestHandleOfThread) {
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
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "example/handle:thread");
}

TEST(ConstsTests, GoodConstEnumMemberReference) {
  TestLibrary library(R"FIDL(library example;

type MyEnum = strict enum : int32 {
    A = 5;
};
const c int32 = MyEnum.A;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodConstBitsMemberReference) {
  TestLibrary library(R"FIDL(library example;

type MyBits = strict bits : uint32 {
    A = 0x00000001;
};
const c uint32 = MyBits.A;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodEnumTypedConstEnumMemberReference) {
  TestLibrary library(R"FIDL(library example;

type MyEnum = strict enum : int32 {
    A = 5;
};
const c MyEnum = MyEnum.A;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodEnumTypedConstBitsMemberReference) {
  TestLibrary library(R"FIDL(library example;

type MyBits = strict bits : uint32 {
    A = 0x00000001;
};
const c MyBits = MyBits.A;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, BadConstDifferentEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

type MyEnum = enum : int32 { VALUE = 1; };
type OtherEnum = enum : int32 { VALUE = 5; };
const c MyEnum = OtherEnum.VALUE;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment,
                                      fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, BadConstDifferentBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

type MyBits = bits : uint32 { VALUE = 0x00000001; };
type OtherBits = bits : uint32 { VALUE = 0x00000004; };
const c MyBits = OtherBits.VALUE;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment,
                                      fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, BadConstAssignPrimitiveToEnum) {
  TestLibrary library(R"FIDL(
library example;

type MyEnum = enum : int32 { VALUE = 1; };
const c MyEnum = 5;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyEnum");
}

TEST(ConstsTests, BadConstAssignPrimitiveToBits) {
  TestLibrary library(R"FIDL(
library example;

type MyBits = bits : uint32 { VALUE = 0x00000001; };
const c MyBits = 5;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "MyBits");
}

TEST(ConstsTests, GoodMaxBoundTest) {
  TestLibrary library(R"FIDL(library example;

const S string:MAX = "";

type Example = struct {
    s string:MAX;
    v vector<bool>:MAX;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodMaxBoundTestConvertToUnbounded) {
  TestLibrary library(R"FIDL(library example;

const A string:MAX = "foo";
const B string = A;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodMaxBoundTestConvertFromUnbounded) {
  TestLibrary library(R"FIDL(library example;

const A string = "foo";
const B string:MAX = A;
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, BadMaxBoundTestAssignToConst) {
  TestLibrary library(R"FIDL(
library example;

const FOO uint32 = MAX;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, BadMaxBoundTestLibraryQualified) {
  SharedAmongstLibraries shared;
  TestLibrary dependency(&shared, "dependency.fidl", R"FIDL(
library dependency;

type Example = struct {};
)FIDL");
  ASSERT_COMPILED(dependency);

  TestLibrary library(&shared, "example.fidl", R"FIDL(
library example;

using dependency;

type Example = struct { s string:dependency.MAX; };
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameNotFound);
}

TEST(ConstsTests, BadParameterizePrimitive) {
  TestLibrary library(R"FIDL(
library example;

const u uint8<string> = 0;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ConstsTests, BadConstTestAssignTypeName) {
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

    TestLibrary library(ss.str());
    ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrExpectedValueButGotType,
                                        fidl::ErrCannotResolveConstantValue);
  }
}

TEST(ConstsTests, BadConstTestAssignBuiltinType) {
  for (auto builtin : {"bool", "uint32", "box", "vector", "byte"}) {
    std::ostringstream ss;
    ss << "library example;\n";
    ss << "const FOO uint32 = " << builtin << ";\n";

    TestLibrary library(ss.str());
    // TODO(fxbug.dev/99665): Should have a better error message.
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotResolveConstantValue);
  }
}

TEST(ConstsTests, BadConstTestAssignBuiltinNonType) {
  for (auto builtin : {"MAX", "HEAD", "optional"}) {
    std::ostringstream ss;
    ss << "library example;\n";
    ss << "const FOO uint32 = " << builtin << ";\n";

    TestLibrary library(ss.str());
    // TODO(fxbug.dev/99665): Should have a better error message.
    ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotResolveConstantValue);
  }
}

TEST(ConstsTests, BadNameCollision) {
  TestLibrary library;
  library.AddFile("bad/fi-0034.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrNameCollision);
}

TEST(ConstsTests, GoodFixNameCollisionRename) {
  TestLibrary library;
  library.AddFile("good/fi-0034-b.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodFixNameCollisionRemove) {
  TestLibrary library;
  library.AddFile("good/fi-0034-a.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ConstsTests, GoodMultiFileConstReference) {
  TestLibrary library;
  library.AddSource("first.fidl", R"FIDL(
library example;

type Protein = struct {
    amino_acids vector<uint64>:SMALL_SIZE;
};
)FIDL");
  library.AddSource("second.fidl", R"FIDL(
library example;

const SMALL_SIZE uint32 = 4;
)FIDL");

  ASSERT_COMPILED(library);
}

TEST(ConstsTests, BadUnknownEnumMemberTest) {
  TestLibrary library(R"FIDL(
library example;

type EnumType = enum : int32 {
    A = 0x00000001;
    B = 0x80;
    C = 0x2;
};

const dee EnumType = EnumType.D;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMemberNotFound);
}

TEST(ConstsTests, BadUnknownBitsMemberTest) {
  TestLibrary library(R"FIDL(
library example;

type BitsType = bits {
    A = 2;
    B = 4;
    C = 8;
};

const dee BitsType = BitsType.D;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMemberNotFound);
}

TEST(ConstsTests, GoodOrOperatorTest) {
  TestLibrary library(R"FIDL(library example;

type MyBits = strict bits : uint8 {
    A = 0x00000001;
    B = 0x00000002;
    C = 0x00000004;
    D = 0x00000008;
};
const bitsValue MyBits = MyBits.A | MyBits.B | MyBits.D;
const Result uint16 = MyBits.A | MyBits.B | MyBits.D;
)FIDL");
  ASSERT_COMPILED(library);

  CheckConstEq<uint16_t>(library, "Result", 11, fidl::flat::Constant::Kind::kBinaryOperator,
                         fidl::flat::ConstantValue::Kind::kUint16);
}

TEST(ConstsTests, BadOrOperatorDifferentTypesTest) {
  TestLibrary library;
  library.AddFile("bad/fi-0065-b.test.fidl");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrTypeCannotBeConvertedToType,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "uint8");
}

TEST(ConstsTests, GoodOrOperatorDifferentTypesTest) {
  TestLibrary library(R"FIDL(library example;

const one uint8 = 0x0001;
const two_fifty_six uint16 = 0x0100;
const two_fifty_seven uint16 = one | two_fifty_six;
)FIDL");
  ASSERT_COMPILED(library);

  CheckConstEq<uint16_t>(library, "two_fifty_seven", 257,
                         fidl::flat::Constant::Kind::kBinaryOperator,
                         fidl::flat::ConstantValue::Kind::kUint16);
}

TEST(ConstsTests, BadOrOperatorNonPrimitiveTypesTest) {
  TestLibrary library(R"FIDL(
library example;

const HI string = "hi";
const THERE string = "there";
const result string = HI | THERE;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrOrOperatorOnNonPrimitiveValue,
                                      fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, GoodOrOperatorParenthesesTest) {
  TestLibrary library(R"FIDL(library example;

type MyBits = strict bits : uint8 {
    A = 0x00000001;
    B = 0x00000002;
    C = 0x00000004;
    D = 0x00000008;
};
const three MyBits = MyBits.A | MyBits.B;
const seven MyBits = three | MyBits.C;
const fifteen MyBits = (three | seven) | MyBits.D;
const bitsValue MyBits = MyBits.A | ( ( (MyBits.A | MyBits.B) | MyBits.D) | MyBits.C);
)FIDL");
  ASSERT_COMPILED(library);

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
  TestLibrary library = TestLibrary(R"FIDL(
library example;

const three uint16 = 3;
const seven uint16 = 7;
const eight uint16 = 8;
const fifteen uint16 = ( three | seven | eight;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind);
}

TEST(ConstsTests, BadOrOperatorMissingLeftParenTest) {
  TestLibrary library = TestLibrary(R"FIDL(
library example;

const three uint16 = 3;
const seven uint16 = 7;
const eight uint16 = 8;
const fifteen uint16 = three | seven | eight );
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrUnexpectedTokenOfKind,
                                      fidl::ErrExpectedDeclaration);
}

TEST(ConstsTests, BadOrOperatorMisplacedParenTest) {
  TestLibrary library = TestLibrary(R"FIDL(
library example;

const three uint16 = 3;
const seven uint16 = 7;
const eight uint16 = 8;
const fifteen uint16 = ( three | seven | ) eight;
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnexpectedToken);
}

TEST(ConstsTests, BadIdentifierConstMismatchedTypesTest) {
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
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "AnotherEnum");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "OneEnum");
}

TEST(ConstsTests, BadEnumBitsConstMismatchedTypesTest) {
  TestLibrary library(R"FIDL(
library example;

type OneEnum = enum {
    A = 1;
};
type AnotherEnum = enum {
    B = 1;
};
const a OneEnum = AnotherEnum.B;
)FIDL");
  ASSERT_ERRORED_TWICE_DURING_COMPILE(library, fidl::ErrMismatchedNameTypeAssignment,
                                      fidl::ErrCannotResolveConstantValue);
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "AnotherEnum");
  ASSERT_SUBSTR(library.errors()[0]->msg.c_str(), "OneEnum");
}

TEST(ConstsTests, BadConstReferencesInvalidConst) {
  // Test all orderings since this previously crashed only when the invalid
  // const (set to 1 instead of a string) was lexicographically smaller.
  for (auto& defs : {
           "const A string = Z; const Z string = 1;",
           "const A string = 1; const Z string = A;",
           "const Z string = A; const A string = 1;",
           "const Z string = 1; const A string = Z;",
       }) {
    std::ostringstream ss;
    ss << "library example;\n";
    ss << defs << "\n";

    TestLibrary library(ss.str());
    ASSERT_FALSE(library.Compile());
    ASSERT_EQ(library.errors().size(), 3);
    EXPECT_ERR(library.errors()[0], fidl::ErrTypeCannotBeConvertedToType);
    EXPECT_ERR(library.errors()[1], fidl::ErrCannotResolveConstantValue);
    EXPECT_ERR(library.errors()[2], fidl::ErrCannotResolveConstantValue);
  }
}

TEST(ConstsTests, GoodDeclaration) {
  TestLibrary library;
  library.AddFile("good/fi-0006.test.fidl");
  ASSERT_COMPILED(library);
}

}  // namespace
