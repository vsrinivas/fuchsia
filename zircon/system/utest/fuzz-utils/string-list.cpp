// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzz-utils/string-list.h>
#include <unittest/unittest.h>

namespace fuzzing {
namespace testing {
namespace {

#define arraysize(x) sizeof(x) / sizeof(x[0])

// Helper function to find expected strings in a list
bool Match(StringList* list, const char** expected, size_t off, size_t len) {
    BEGIN_HELPER;
    EXPECT_EQ(list->length(), len);
    const char* elem = list->first();
    for (size_t i = 0; i < len; ++i) {
        ASSERT_NONNULL(elem);
        EXPECT_STR_EQ(elem, expected[off + i]);
        elem = list->next();
    }
    EXPECT_NULL(elem);
    END_HELPER;
}

bool TestEmpty() {
    BEGIN_TEST;
    StringList list;

    EXPECT_TRUE(list.is_empty());
    EXPECT_NULL(list.first());
    EXPECT_NULL(list.next());

    END_TEST;
}

bool TestPushFrontAndBack() {
    BEGIN_TEST;
    StringList list;
    const char* expected[] = {"", "foo", "bar", "baz", ""};

    // Strings can be pushed from either end
    list.push_front("bar");
    list.push_back("baz");
    list.push_front("foo");
    EXPECT_TRUE(Match(&list, expected, 1, 3));

    // Empty strings are fine
    list.push_front("");
    list.push_back("");
    EXPECT_TRUE(Match(&list, expected, 0, 5));

    // Null strings are ignored
    list.push_front(nullptr);
    list.push_back(nullptr);
    EXPECT_TRUE(Match(&list, expected, 0, 5));

    // Test the new constructor
    StringList list2(expected, arraysize(expected));
    EXPECT_TRUE(Match(&list, expected, 0, 5));

    END_TEST;
}

bool TestKeepIf() {
    BEGIN_TEST;
    StringList list;
    const char* original[] = {"",     "foo",   "bar",    "baz",    "qux",
                              "quux", "corge", "grault", "garply", "waldo",
                              "fred", "plugh", "xyzzy",  "thud",   ""};

    const char* expected1[] = {"bar", "corge", "grault", "garply", "plugh"};

    const char* expected2[] = {"corge", "grault", "garply", "plugh"};

    const char* expected3[] = {"garply"};

    for (size_t i = 0; i < arraysize(original); ++i) {
        list.push_back(original[i]);
    }

    // Null string has no effect
    list.keep_if(nullptr);
    EXPECT_TRUE(Match(&list, original, 0, arraysize(original)));

    // Empty string matches everything
    list.keep_if("");
    EXPECT_TRUE(Match(&list, original, 0, arraysize(original)));

    // Match a string
    list.keep_if("g");
    EXPECT_TRUE(Match(&list, expected2, 0, arraysize(expected2)));

    // Match a string that would have matched elements in the original list
    list.keep_if("ar");
    EXPECT_TRUE(Match(&list, expected3, 0, arraysize(expected3)));

    // Use a string that doesn't match anything
    list.keep_if("zzz");
    EXPECT_TRUE(list.is_empty());

    // Reset and apply both matches at once with logical-or
    StringList substrs;
    substrs.push_back("g");
    substrs.push_back("ar");

    list.clear();
    for (size_t i = 0; i < arraysize(original); ++i) {
        list.push_back(original[i]);
    }
    list.keep_if_any(&substrs);
    EXPECT_TRUE(Match(&list, expected1, 0, arraysize(expected1)));

    // Reset and apply both matches at once with logical-and
    list.clear();
    for (size_t i = 0; i < arraysize(original); ++i) {
        list.push_back(original[i]);
    }
    list.keep_if_all(&substrs);
    EXPECT_TRUE(Match(&list, expected3, 0, arraysize(expected3)));

    END_TEST;
}

bool TestEraseIf() {
    BEGIN_TEST;
    StringList list;
    const char* original[] = {"", "foo", "bar", "baz", ""};

    const char* expected1[] = {"", "foo", "baz", ""};

    const char* expected2[] = {"foo", "baz"};

    for (size_t i = 0; i < sizeof(original) / sizeof(original[0]); ++i) {
        list.push_back(original[i]);
    }

    // Null and empty strings have no effect
    list.erase_if(nullptr);
    EXPECT_TRUE(Match(&list, original, 0, arraysize(original)));

    // Use a string that doesn't match anything
    list.erase_if("zzz");
    EXPECT_TRUE(Match(&list, original, 0, arraysize(original)));

    // Match a string
    list.erase_if("bar");
    EXPECT_TRUE(Match(&list, expected1, 0, arraysize(expected1)));

    // Idempotent
    list.erase_if("bar");
    EXPECT_TRUE(Match(&list, expected1, 0, arraysize(expected1)));

    // Able to erase empty strings
    list.erase_if("");
    EXPECT_TRUE(Match(&list, expected2, 0, arraysize(expected2)));

    END_TEST;
}

bool TestClear() {
    BEGIN_TEST;

    StringList list;
    list.push_front("bar");

    EXPECT_NONNULL(list.first());
    list.clear();
    EXPECT_NULL(list.next());
    EXPECT_NULL(list.first());
    EXPECT_EQ(list.length(), 0);

    END_TEST;
}

BEGIN_TEST_CASE(StringListTest)
RUN_TEST(TestEmpty)
RUN_TEST(TestPushFrontAndBack)
RUN_TEST(TestKeepIf)
RUN_TEST(TestEraseIf)
RUN_TEST(TestClear)
END_TEST_CASE(StringListTest)

} // namespace
} // namespace testing
} // namespace fuzzing
