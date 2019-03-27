// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

// #include <fidl/flat_ast.h>
// #include <fidl/lexer.h>
// #include <fidl/parser.h>
// #include <fidl/source_file.h>

#include "test_library.h"

namespace {

bool CheckConstEq(TestLibrary& library, const std::string& name, uint32_t expected_value) {
    BEGIN_HELPER;

    auto const_decl = library.LookupConstant(name);
    ASSERT_NOT_NULL(const_decl);
    ASSERT_EQ(fidl::flat::Constant::Kind::kLiteral, const_decl->value->kind);
    ASSERT_EQ(fidl::flat::ConstantValue::Kind::kUint32, const_decl->value->Value().kind);
    auto numeric_const_value = static_cast<const fidl::flat::NumericConstantValue<uint32_t>&>(
        const_decl->value->Value());
    EXPECT_EQ(expected_value, static_cast<uint32_t>(numeric_const_value));

    END_HELPER;
}

bool LiteralsTest() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const uint32 C_SIMPLE   = 11259375;
const uint32 C_HEX_S    = 0xABCDEF;
const uint32 C_HEX_L    = 0XABCDEF;
const uint32 C_BINARY_S = 0b101010111100110111101111;
const uint32 C_BINARY_L = 0B101010111100110111101111;
)FIDL");
    ASSERT_TRUE(library.Compile());

    EXPECT_TRUE(CheckConstEq(library, "C_SIMPLE", 11259375));
    EXPECT_TRUE(CheckConstEq(library, "C_HEX_S", 11259375));
    EXPECT_TRUE(CheckConstEq(library, "C_HEX_L", 11259375));
    EXPECT_TRUE(CheckConstEq(library, "C_BINARY_S", 11259375));
    EXPECT_TRUE(CheckConstEq(library, "C_BINARY_L", 11259375));

    END_TEST;
}

bool GoodConstTestBool() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const bool c = false;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool BadConstTestBoolWithString() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const bool c = "foo";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "\"foo\" cannot be interpreted as type bool");

    END_TEST;
}

bool BadConstTestBoolWithNumeric() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const bool c = 6;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "6 cannot be interpreted as type bool");

    END_TEST;
}

bool GoodConstTestInt32() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const int32 c = 42;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool GoodConstTestInt32FromOtherConst() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const int32 b = 42;
const int32 c = b;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool BadConstTestInt32WithString() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const int32 c = "foo";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "\"foo\" cannot be interpreted as type int32");

    END_TEST;
}

bool BadConstTestInt32WithBool() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const int32 c = true;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "true cannot be interpreted as type int32");

    END_TEST;
}

bool GoodConstTesUint64() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const int64 a = 42;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool GoodConstTestUint64FromOtherUint32() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const uint32 a = 42;
const uint64 b = a;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool BadConstTestUint64Negative() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const uint64 a = -42;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "-42 cannot be interpreted as type uint64");

    END_TEST;
}

bool BadConstTestUint64Overflow() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const uint64 a = 18446744073709551616;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "18446744073709551616 cannot be interpreted as type uint64");

    END_TEST;
}

bool GoodConstTestFloat32() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const float32 b = 1.61803;
const float32 c = -36.46216;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool GoodConstTestFloat32HighLimit() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const float32 hi = 3.402823e38;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool GoodConstTestFloat32LowLimit() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const float32 lo = -3.40282e38;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool BadConstTestFloat32HighLimit() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const float32 hi = 3.41e38;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "3.41e38 cannot be interpreted as type float32");

    END_TEST;
}

bool BadConstTestFloat32LowLimit() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const float32 b = -3.41e38;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "-3.41e38 cannot be interpreted as type float32");

    END_TEST;
}

bool GoodConstTestString() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string:4 c = "four";
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool GoodConstTestStringFromOtherConst() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string:4 c = "four";
const string:5 d = c;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool BadConstTestStringWithNumeric() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string c = 4;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "4 cannot be interpreted as type string");

    END_TEST;
}

bool BadConstTestStringWithBool() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string c = true;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "true cannot be interpreted as type string");

    END_TEST;
}

bool BadConstTestStringWithStringTooLong() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string:4 c = "hello";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(),
                   "\"hello\" (string:5) exceeds the size bound of type string:4");

    END_TEST;
}

bool GoodConstTestUsing() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using foo = int32;
const foo c = 2;
)FIDL");
    ASSERT_TRUE(library.Compile());

    END_TEST;
}

bool BadConstTestUsingWithInconvertibleValue() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

using foo = int32;
const foo c = "nope";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "\"nope\" cannot be interpreted as type int32");

    END_TEST;
}

bool BadConstTestNullableString() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const string? c = "";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type string?");

    END_TEST;
}

bool BadConstTestEnum() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

enum MyEnum : int32 { A = 5; };
const MyEnum c = "";
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type example/MyEnum");

    END_TEST;
}

bool BadConstTestArray() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const array<int32>:2 c = -1;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type array<int32>:2");

    END_TEST;
}

bool BadConstTestVector() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const vector<int32>:2 c = -1;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type vector<int32>:2");

    END_TEST;
}

bool BadConstTestHandleOfThread() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

const handle<thread> c = -1;
)FIDL");
    ASSERT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_GE(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid constant type handle<thread>");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(consts_tests)

RUN_TEST(LiteralsTest)

RUN_TEST(GoodConstTestBool)
RUN_TEST(BadConstTestBoolWithString)
RUN_TEST(BadConstTestBoolWithNumeric)

RUN_TEST(GoodConstTestInt32)
RUN_TEST(GoodConstTestInt32FromOtherConst)
RUN_TEST(BadConstTestInt32WithString)
RUN_TEST(BadConstTestInt32WithBool)

RUN_TEST(GoodConstTesUint64)
RUN_TEST(GoodConstTestUint64FromOtherUint32)
RUN_TEST(BadConstTestUint64Negative)
RUN_TEST(BadConstTestUint64Overflow)

RUN_TEST(GoodConstTestFloat32);
RUN_TEST(GoodConstTestFloat32HighLimit)
RUN_TEST(GoodConstTestFloat32LowLimit)
RUN_TEST(BadConstTestFloat32HighLimit)
RUN_TEST(BadConstTestFloat32LowLimit)

RUN_TEST(GoodConstTestString)
RUN_TEST(GoodConstTestStringFromOtherConst)
RUN_TEST(BadConstTestStringWithNumeric)
RUN_TEST(BadConstTestStringWithBool)
RUN_TEST(BadConstTestStringWithStringTooLong)

RUN_TEST(GoodConstTestUsing)
RUN_TEST(BadConstTestUsingWithInconvertibleValue)

RUN_TEST(BadConstTestNullableString)
RUN_TEST(BadConstTestEnum)
RUN_TEST(BadConstTestArray)
RUN_TEST(BadConstTestVector)
RUN_TEST(BadConstTestHandleOfThread)

END_TEST_CASE(consts_tests)
