// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuzz-utils/string-list.h>
#include <unittest/unittest.h>

namespace fuzzing {
namespace testing {
namespace {

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
    StringList list2(expected, sizeof(expected) / sizeof(expected[0]));
    EXPECT_TRUE(Match(&list, expected, 0, 5));

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
RUN_TEST(TestClear)
END_TEST_CASE(StringListTest)

} // namespace
} // namespace testing
} // namespace fuzzing
