// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <locale.h>

#include <fidl/flat_ast.h>
#include <fidl/lexer.h>
#include <fidl/parser.h>
#include <fidl/source_file.h>

#include "test_library.h"

namespace {

// Test that an invalid compound identifier fails parsing. Regression
// test for FIDL-263.
bool bad_compound_identifier_test() {
    BEGIN_TEST;

    // The leading 0 in the library name causes parsing an Identifier
    // to fail, and then parsing a CompoundIdentifier to fail.
    TestLibrary library(R"FIDL(
library 0fidl.test.badcompoundidentifier;
)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "unexpected token");

    END_TEST;
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
bool parsing_reserved_words_in_struct_test() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

struct InStruct {
    bool as;
    bool library;
    bool using;

    bool array;
    bool handle;
    bool request;
    bool string;
    bool vector;

    bool bool;
    bool int8;
    bool int16;
    bool int32;
    bool int64;
    bool uint8;
    bool uint16;
    bool uint32;
    bool uint64;
    bool float32;
    bool float64;

    bool true;
    bool false;

    bool reserved;
};
)FIDL");
    EXPECT_TRUE(library.Compile());

    END_TEST;
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
bool parsing_reserved_words_in_interface_test() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library example;

interface InInterface {
    01: as(bool as);
    02: library(bool library);
    03: using(bool using);

    11: array(bool array);
    12: handle(bool handle);
    13: request(bool request);
    14: string(bool string);
    15: vector(bool vector);

    31: bool(bool bool);
    32: int8(bool int8);
    33: int16(bool int16);
    34: int32(bool int32);
    35: int64(bool int64);
    36: uint8(bool uint8);
    37: uint16(bool uint16);
    38: uint32(bool uint32);
    39: uint64(bool uint64);
    40: float32(bool float32);
    41: float64(bool float64);

    51: true(bool true);
    52: false(bool false);

    61: reserved(bool reserved);
};
)FIDL");
    EXPECT_TRUE(library.Compile());

    END_TEST;
}

bool bad_char_at_sign_test() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library test;

struct Test {
    uint8 @uint8;
};
)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid character '@'");

    END_TEST;
}

bool bad_char_slash_test() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library test;

struct Test / {
    uint8 uint8;
};
)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid character '/'");

    END_TEST;
}

bool bad_identifier_test() {
    BEGIN_TEST;

    TestLibrary library(R"FIDL(
library test;

struct test_ {
    uint8 uint8;
};
)FIDL");
    EXPECT_FALSE(library.Compile());
    auto errors = library.errors();
    ASSERT_EQ(errors.size(), 1);
    ASSERT_STR_STR(errors[0].c_str(), "invalid identifier 'test_'");

    END_TEST;
}

class LocaleSwapper {
public:
    explicit LocaleSwapper(const char* new_locale) {
        old_locale_ = setlocale(LC_ALL, new_locale);
    }
    ~LocaleSwapper() {
        setlocale(LC_ALL, old_locale_);
    }

private:
    const char* old_locale_;
};

static bool invalid_character_test(void) {
    BEGIN_TEST;

    class InvalidCharacterLibrary : public TestLibrary {
    public:
        InvalidCharacterLibrary()
        : TestLibrary("invalid.character.fidl", R"FIDL(
library fidl.test.maxbytes;

// This is all alphanumeric in the appropriate locale, but not a valid
// identifier.
struct ß {
    int32 x;
};

)FIDL") {}
    } test_library;

    {
        LocaleSwapper("de_DE.iso88591");
        EXPECT_FALSE(test_library.Compile());
    }

    const auto& errors = test_library.errors();
    EXPECT_NE(errors.size(), 0);
    ASSERT_STR_STR(errors[0].data(), "invalid character");

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(parser_tests);
RUN_TEST(bad_compound_identifier_test);
RUN_TEST(parsing_reserved_words_in_struct_test);
RUN_TEST(parsing_reserved_words_in_interface_test);
RUN_TEST(bad_char_at_sign_test);
RUN_TEST(bad_char_slash_test);
RUN_TEST(bad_identifier_test);
RUN_TEST(invalid_character_test);
END_TEST_CASE(parser_tests);
