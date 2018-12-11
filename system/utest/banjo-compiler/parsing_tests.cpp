// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include <banjo/flat_ast.h>
#include <banjo/lexer.h>
#include <banjo/parser.h>
#include <banjo/source_file.h>

#include "test_library.h"

namespace {

// Test that an invalid compound identifier fails parsing. Regression
// test for BANJO-263.
bool bad_compound_identifier_test() {
    BEGIN_TEST;

    // The leading 0 in the library name causes parsing an Identifier
    // to fail, and then parsing a CompoundIdentifier to fail.
    TestLibrary library(R"BANJO(
library 0banjo.test.badcompoundidentifier;
)BANJO");
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

    TestLibrary library(R"BANJO(
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
)BANJO");
    EXPECT_TRUE(library.Compile());

    END_TEST;
}

// Test that otherwise reserved words can be appropriarely parsed when context
// is clear.
bool parsing_reserved_words_in_interface_test() {
    BEGIN_TEST;

    TestLibrary library(R"BANJO(
library example;

interface InInterface {
    as(bool as);
    library(bool library);
    using(bool using);

    array(bool array);
    handle(bool handle);
    request(bool request);
    string(bool string);
    vector(bool vector);

    bool(bool bool);
    int8(bool int8);
    int16(bool int16);
    int32(bool int32);
    int64(bool int64);
    uint8(bool uint8);
    uint16(bool uint16);
    uint32(bool uint32);
    uint64(bool uint64);
    float32(bool float32);
    float64(bool float64);

    true(bool true);
    false(bool false);

    reserved(bool reserved);
};
)BANJO");
    EXPECT_TRUE(library.Compile());

    END_TEST;
}

} // namespace

BEGIN_TEST_CASE(parser_tests);
RUN_TEST(bad_compound_identifier_test);
RUN_TEST(parsing_reserved_words_in_struct_test);
RUN_TEST(parsing_reserved_words_in_interface_test);
END_TEST_CASE(parser_tests);
