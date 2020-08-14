// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sstream>

#include <zxtest/zxtest.h>

#include "error_test.h"
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

TEST(ConstsTests, LiteralsTest) {
  TestLibrary library(R"FIDL(
library example;

const uint32 C_SIMPLE   = 11259375;
const uint32 C_HEX_S    = 0xABCDEF;
const uint32 C_HEX_L    = 0XABCDEF;
const uint32 C_BINARY_S = 0b101010111100110111101111;
const uint32 C_BINARY_L = 0B101010111100110111101111;
)FIDL");
  ASSERT_TRUE(library.Compile());

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
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, BadConstTestBoolWithString) {
  TestLibrary library(R"FIDL(
library example;

const bool c = "foo";
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "\"foo\"");
}

TEST(ConstsTests, BadConstTestBoolWithNumeric) {
  TestLibrary library(R"FIDL(
library example;

const bool c = 6;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "6");
}

TEST(ConstsTests, GoodConstTestInt32) {
  TestLibrary library(R"FIDL(
library example;

const int32 c = 42;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodConstTestInt32FromOtherConst) {
  TestLibrary library(R"FIDL(
library example;

const int32 b = 42;
const int32 c = b;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, BadConstTestInt32WithString) {
  TestLibrary library(R"FIDL(
library example;

const int32 c = "foo";
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "\"foo\"");
}

TEST(ConstsTests, BadConstTestInt32WithBool) {
  TestLibrary library(R"FIDL(
library example;

const int32 c = true;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "true");
}

TEST(ConstsTests, GoodConstTesUint64) {
  TestLibrary library(R"FIDL(
library example;

const int64 a = 42;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodConstTestUint64FromOtherUint32) {
  TestLibrary library(R"FIDL(
library example;

const uint32 a = 42;
const uint64 b = a;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, BadConstTestUint64Negative) {
  TestLibrary library(R"FIDL(
library example;

const uint64 a = -42;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "-42");
}

TEST(ConstsTests, BadConstTestUint64Overflow) {
  TestLibrary library(R"FIDL(
library example;

const uint64 a = 18446744073709551616;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "18446744073709551616");
}

TEST(ConstsTests, GoodConstTestFloat32) {
  TestLibrary library(R"FIDL(
library example;

const float32 b = 1.61803;
const float32 c = -36.46216;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodConstTestFloat32HighLimit) {
  TestLibrary library(R"FIDL(
library example;

const float32 hi = 3.402823e38;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodConstTestFloat32LowLimit) {
  TestLibrary library(R"FIDL(
library example;

const float32 lo = -3.40282e38;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, BadConstTestFloat32HighLimit) {
  TestLibrary library(R"FIDL(
library example;

const float32 hi = 3.41e38;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "3.41e38");
}

TEST(ConstsTests, BadConstTestFloat32LowLimit) {
  TestLibrary library(R"FIDL(
library example;

const float32 b = -3.41e38;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "-3.41e38");
}

TEST(ConstsTests, GoodConstTestString) {
  TestLibrary library(R"FIDL(
library example;

const string:4 c = "four";
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodConstTestStringFromOtherConst) {
  TestLibrary library(R"FIDL(
library example;

const string:4 c = "four";
const string:5 d = c;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

// TODO(fxbug.dev/37314): Both declarations should have the same type.
TEST(ConstsTests, GoodConstTestStringShouldHaveInferredBounds) {
  TestLibrary library(R"FIDL(
library example;

const string INFERRED = "four";
const string:4 EXPLICIT = "four";

)FIDL");
  ASSERT_TRUE(library.Compile());

  auto inferred_const = library.LookupConstant("INFERRED");
  ASSERT_NOT_NULL(inferred_const->type_ctor->type);
  ASSERT_EQ(inferred_const->type_ctor->type->kind, fidl::flat::Type::Kind::kString);
  auto inferred_string_type =
      static_cast<const fidl::flat::StringType*>(inferred_const->type_ctor->type);
  ASSERT_NOT_NULL(inferred_string_type->max_size);
  ASSERT_EQ(static_cast<uint32_t>(*inferred_string_type->max_size), 4294967295u);

  auto explicit_const = library.LookupConstant("EXPLICIT");
  ASSERT_NOT_NULL(explicit_const->type_ctor->type);
  ASSERT_EQ(explicit_const->type_ctor->type->kind, fidl::flat::Type::Kind::kString);
  auto explicit_string_type =
      static_cast<const fidl::flat::StringType*>(explicit_const->type_ctor->type);
  ASSERT_NOT_NULL(explicit_string_type->max_size);
  ASSERT_EQ(static_cast<uint32_t>(*explicit_string_type->max_size), 4u);
}

TEST(ConstsTests, BadConstTestStringWithNumeric) {
  TestLibrary library(R"FIDL(
library example;

const string c = 4;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "4");
}

TEST(ConstsTests, BadConstTestStringWithBool) {
  TestLibrary library(R"FIDL(
library example;

const string c = true;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "true");
}

TEST(ConstsTests, BadConstTestStringWithStringTooLong) {
  TestLibrary library(R"FIDL(
library example;

const string:4 c = "hello";
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrStringConstantExceedsSizeBound);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "\"hello\"");
}

TEST(ConstsTests, GoodConstTestUsing) {
  TestLibrary library(R"FIDL(
library example;

using foo = int32;
const foo c = 2;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, BadConstTestUsingWithInconvertibleValue) {
  TestLibrary library(R"FIDL(
library example;

using foo = int32;
const foo c = "nope";
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "\"nope\"");
}

TEST(ConstsTests, BadConstTestNullableString) {
  TestLibrary library(R"FIDL(
library example;

const string? c = "";
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "string?");
}

TEST(ConstsTests, BadConstTestArray) {
  TestLibrary library(R"FIDL(
library example;

const array<int32>:2 c = -1;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "array<int32>:2");
}

TEST(ConstsTests, BadConstTestVector) {
  TestLibrary library(R"FIDL(
library example;

const vector<int32>:2 c = -1;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "vector<int32>:2");
}

TEST(ConstsTests, BadConstTestHandleOfThread) {
  TestLibrary library(R"FIDL(
library example;

const handle<thread> c = -1;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrInvalidConstantType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "handle<thread>");
}

TEST(ConstsTests, GoodConstEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };
const int32 c = MyEnum.A;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodConstBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };
const uint32 c = MyBits.A;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodEnumTypedConstEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };
const MyEnum c = MyEnum.A;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodEnumTypedConstBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { A = 0x00000001; };
const MyBits c = MyBits.A;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, BadConstDifferentEnumMemberReference) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { VALUE = 1; };
enum OtherEnum : int32 { VALUE = 5; };
const MyEnum c = OtherEnum.VALUE;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMismatchedNameTypeAssignment);
}

TEST(ConstsTests, BadConstDifferentBitsMemberReference) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { VALUE = 0x00000001; };
bits OtherBits : uint32 { VALUE = 0x00000004; };
const MyBits c = OtherBits.VALUE;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrMismatchedNameTypeAssignment);
}

TEST(ConstsTests, BadConstAssignPrimitiveToEnum) {
  TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { VALUE = 1; };
const MyEnum c = 5;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "MyEnum");
}

TEST(ConstsTests, BadConstAssignPrimitiveToBits) {
  TestLibrary library(R"FIDL(
library example;

bits MyBits : uint32 { VALUE = 0x00000001; };
const MyBits c = 5;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrConstantCannotBeInterpretedAsType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "MyBits");
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
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodMaxBoundTestConvertToUnbounded) {
  TestLibrary library(R"FIDL(
library example;

const string:MAX A = "foo";
const string B = A;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, GoodMaxBoundTestConvertFromUnbounded) {
  TestLibrary library(R"FIDL(
library example;

const string A = "foo";
const string:MAX B = A;
)FIDL");
  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, BadMaxBoundTestAssignToConst) {
  TestLibrary library(R"FIDL(
library example;

const uint32 FOO = MAX;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrFailedConstantLookup);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "MAX");
}

TEST(ConstsTests, BadMaxBoundTestLibraryQualified) {
  SharedAmongstLibraries shared;
  TestLibrary dependency("dependency.fidl", R"FIDL(
library dependency;

struct Example {};
)FIDL",
                         &shared);
  ASSERT_TRUE(dependency.Compile());

  TestLibrary library(R"FIDL(
library example;

using dependency;

struct Example { string:dependency.MAX s; };
)FIDL");
  ASSERT_TRUE(library.AddDependentLibrary(std::move(dependency)));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCouldNotParseSizeBound);
}

TEST(ConstsTests, BadParameterizePrimitive) {
  TestLibrary library(R"FIDL(
library example;

const uint8<string> u = 0;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrCannotBeParameterized);
}

TEST(ConstsTests, BadConstTestAssignTypeName) {
  for (auto type_declaration : {
           "struct Example {};",
           "table Example {};",
           "service Example {};",
           "protocol Example {};",
           "bits Example { A = 1; };",
           "enum Example { A = 1; };",
           "union Example { 1: bool A; };",
           "using Example = string;",
       }) {
    std::ostringstream ss;
    ss << "library example;\n";
    ss << type_declaration << "\n";
    ss << "const uint32 FOO = Example;\n";

    TestLibrary library(ss.str());
    ASSERT_FALSE(library.Compile());
    const auto& errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_ERR(errors[0], fidl::ErrExpectedValueButGotType);
  }
}

TEST(ConstsTests, BadNameCollision) {
  TestLibrary library(R"FIDL(
library example;

const uint8 FOO = 0;
const uint8 FOO = 1;
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrNameCollision);
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

  ASSERT_TRUE(library.Compile());
}

TEST(ConstsTests, UnknownEnumMemberTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum EnumType : int32 {
    A = 0x00000001;
    B = 0x80;
    C = 0x2;
};

const EnumType dee = EnumType.D;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnknownEnumMember);
  ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, UnknownBitsMemberTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

bits BitsType {
    A = 2;
    B = 4;
    C = 8;
};

const BitsType dee = BitsType.D;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrUnknownBitsMember);
  ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, OrOperatorTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

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
  ASSERT_TRUE(library.Compile());

  CheckConstEq<uint16_t>(library, "Result", 11, fidl::flat::Constant::Kind::kBinaryOperator,
                         fidl::flat::ConstantValue::Kind::kUint16);
}

TEST(ConstsTests, BadOrOperatorDifferentTypesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

const uint8 one = 0x0001;
const uint16 two_fifty_six = 0x0100;
const uint8 two_fifty_seven = one | two_fifty_six;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrCannotConvertConstantToType);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "uint8");
  ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, GoodOrOperatorDifferentTypesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

const uint8 one = 0x0001;
const uint16 two_fifty_six = 0x0100;
const uint16 two_fifty_seven = one | two_fifty_six;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_TRUE(library.Compile());

  CheckConstEq<uint16_t>(library, "two_fifty_seven", 257,
                         fidl::flat::Constant::Kind::kBinaryOperator,
                         fidl::flat::ConstantValue::Kind::kUint16);
}

TEST(ConstsTests, BadOrOperatorNonPrimitiveTypesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

const string HI = "hi";
const string THERE = "there";
const string result = HI | THERE;
  )FIDL",
                      std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrOrOperatorOnNonPrimitiveValue);
  ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, GoodOrOperatorParenthesesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

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
  ASSERT_TRUE(library.Compile());

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
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library = TestLibrary(R"FIDL(
library example;

const uint16 three = 3;
const uint16 seven = 7;
const uint16 eight = 8;
const uint16 fifteen = ( three | seven | eight;
)FIDL",
                                    std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
}

TEST(ConstsTests, BadOrOperatorMissingLeftParenTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library = TestLibrary(R"FIDL(
library example;

const uint16 three = 3;
const uint16 seven = 7;
const uint16 eight = 8;
const uint16 fifteen = three | seven | eight );
)FIDL",
                                    std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedTokenOfKind);
}

TEST(ConstsTests, BadOrOperatorMisplacedParenTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library = TestLibrary(R"FIDL(
library example;

const uint16 three = 3;
const uint16 seven = 7;
const uint16 eight = 8;
const uint16 fifteen = ( three | seven | ) eight;
)FIDL",
                                    std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 1);
  ASSERT_ERR(errors[0], fidl::ErrUnexpectedToken);
}

TEST(ConstsTests, IdentifierConstMismatchedTypesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum OneEnum {
    A = 1;
};
enum AnotherEnum {
    B = 1;
};
const OneEnum a = OneEnum.A;
const AnotherEnum b = a;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrMismatchedNameTypeAssignment);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "AnotherEnum");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "OneEnum");
  ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue);
}

TEST(ConstsTests, EnumBitsConstMismatchedTypesTest) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kEnableHandleRights);

  TestLibrary library(R"FIDL(
library example;

enum OneEnum {
    A = 1;
};
enum AnotherEnum {
    B = 1;
};
const OneEnum a = AnotherEnum.B;
)FIDL",
                      std::move(experimental_flags));
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_GE(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrMismatchedNameTypeAssignment);
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "AnotherEnum");
  ASSERT_SUBSTR(errors[0]->msg.c_str(), "OneEnum");
  ASSERT_ERR(errors[1], fidl::ErrCannotResolveConstantValue);
}

}  // namespace
