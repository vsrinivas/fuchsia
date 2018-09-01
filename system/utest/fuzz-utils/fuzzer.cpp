// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <unittest/unittest.h>

#include "fuzzer-fixture.h"
#include "test-fuzzer.h"

namespace fuzzing {
namespace testing {
namespace {

// See fuzzer-fixture.cpp for the location and contents of test files.

bool TestSetOption() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.InitZircon());

    EXPECT_NE(ZX_OK, test.SetOption("", "value1"));
    EXPECT_NE(ZX_OK, test.SetOption("key1", ""));

    // Value isn't set
    const char* value = test.GetOption("key1");
    EXPECT_NULL(value);

    // Empty options are ignored
    EXPECT_EQ(ZX_OK, test.SetOption("", ""));
    EXPECT_EQ(ZX_OK, test.SetOption(""));
    EXPECT_EQ(ZX_OK, test.SetOption("# A comment"));
    EXPECT_EQ(ZX_OK, test.SetOption("   # A comment with leading whitespace"));

    // Set some values normally
    EXPECT_EQ(ZX_OK, test.SetOption("key1", "value1"));
    EXPECT_EQ(ZX_OK, test.SetOption("key2", "value2"));
    EXPECT_EQ(ZX_OK, test.SetOption("key3=value3"));
    EXPECT_EQ(ZX_OK, test.SetOption("\t -key4 \t=\t value4 \t# A comment"));

    // Check values
    value = test.GetOption("key1");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value1");

    value = test.GetOption("key2");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value2");

    value = test.GetOption("key3");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value3");

    value = test.GetOption("key4");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value4");

    // Replace each option
    EXPECT_EQ(ZX_OK, test.SetOption("key3", "value4"));
    EXPECT_EQ(ZX_OK, test.SetOption("key2=value3"));
    EXPECT_EQ(ZX_OK, test.SetOption(" \t-key1\t = \tvalue2\t # A comment"));
    EXPECT_EQ(ZX_OK, test.SetOption("key4", "value1"));

    // Check values
    value = test.GetOption("key1");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value2");

    value = test.GetOption("key2");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value3");

    value = test.GetOption("key3");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value4");

    value = test.GetOption("key4");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value1");

    // Must be key value pair
    EXPECT_NE(ZX_OK, test.SetOption("key1", ""));
    EXPECT_NE(ZX_OK, test.SetOption("", "value2"));
    EXPECT_NE(ZX_OK, test.SetOption("key3"));
    EXPECT_NE(ZX_OK, test.SetOption("key5=#value5"));

    END_TEST;
}

BEGIN_TEST_CASE(FuzzerTest)
RUN_TEST(TestSetOption)
END_TEST_CASE(FuzzerTest)

} // namespace
} // namespace testing
} // namespace fuzzing
