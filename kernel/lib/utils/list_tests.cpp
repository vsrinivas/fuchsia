// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <err.h>
#include <unittest.h>
#include <utils/intrusive_double_list.h>
#include <utils/intrusive_single_list.h>
#include <utils/list_utils.h>

class TestValue {
public:
    TestValue(int val) : val_(val) {}
    int value() const { return val_; }

private:
    int val_;
};

class Foo : public TestValue {
public:
    Foo(int val) : TestValue(val), next_(nullptr), prev_(nullptr) {}

    Foo* list_prev() { return prev_; }
    const Foo* list_prev() const { return prev_; }

    Foo* list_next() { return next_; }
    const Foo* list_next() const { return next_; }

    void list_set_prev(Foo* prev) { prev_ = prev; }
    void list_set_next(Foo* next) { next_ = next; }

private:
    Foo* next_;
    Foo* prev_;
};

static bool doubly_linked_one_list_test(void* context) {
    BEGIN_TEST;
    const size_t count = 5;

    Foo baz[count] = {1, 2, 3, 4, 5};

    utils::DoublyLinkedList<Foo> dlist;

    EXPECT_EQ(0U, dlist.size_slow(), "");
    EXPECT_TRUE(dlist.pop_front() == nullptr, "");
    EXPECT_TRUE(dlist.pop_back() == nullptr, "");

    dlist.push_front(&baz[0]);
    EXPECT_EQ(1U, dlist.size_slow(), "");

    EXPECT_TRUE(dlist.pop_back() == &baz[0], "");
    EXPECT_EQ(0U, dlist.size_slow(), "");
    EXPECT_TRUE(dlist.is_empty(), "");

    dlist.push_back(&baz[1]);
    EXPECT_EQ(1U, dlist.size_slow(), "");

    EXPECT_TRUE(dlist.pop_front() == &baz[1], "");
    EXPECT_EQ(0U, dlist.size_slow(), "");
    EXPECT_TRUE(dlist.is_empty(), "");

    dlist.push_back(&baz[0]);
    dlist.push_back(&baz[1]);
    EXPECT_TRUE(dlist.pop_front() == &baz[0], "");
    EXPECT_TRUE(dlist.pop_front() == &baz[1], "");
    EXPECT_EQ(0U, dlist.size_slow(), "");

    for (int ix = 0; ix != count; ++ix) {
        dlist.push_front(&baz[ix]);
    }
    EXPECT_EQ(5U, dlist.size_slow(), "");

    int sum = 0;
    for_each(&dlist, [&sum](Foo* b) { sum += b->value(); });
    EXPECT_EQ(15, sum, "");

    int prev = 6;
    for (int ix = 0; ix != count; ++ix) {
        auto baz = dlist.pop_front();
        EXPECT_TRUE(prev > baz->value(), "");
        prev = baz->value();
    }

    EXPECT_EQ(0U, dlist.size_slow(), "");

    for (int ix = 0; ix != count; ++ix) {
        dlist.push_back(&baz[ix]);
    }
    EXPECT_EQ(5U, dlist.size_slow(), "");

    utils::DoublyLinkedList<Foo> dlist2;
    dlist2.swap(dlist);
    EXPECT_EQ(0U, dlist.size_slow(), "");
    EXPECT_EQ(5U, dlist2.size_slow(), "");
    EXPECT_TRUE(dlist2.first() == &baz[0], "");
    EXPECT_TRUE(dlist2.last() == &baz[4], "");
    dlist2.swap(dlist);
    EXPECT_EQ(5U, dlist.size_slow(), "");
    EXPECT_EQ(0U, dlist2.size_slow(), "");
    EXPECT_TRUE(dlist.first() == &baz[0], "");
    EXPECT_TRUE(dlist.last() == &baz[4], "");

    prev = 6;
    for (int ix = 0; ix != count; ++ix) {
        auto baz = dlist.pop_back();
        EXPECT_TRUE(prev > baz->value(), "");
        prev = baz->value();
    }

    EXPECT_EQ(0U, dlist.size_slow(), "");

    dlist.push_back(&baz[2]);
    dlist.push_back(&baz[3]);
    dlist.remove(dlist.first());

    EXPECT_EQ(1U, dlist.size_slow(), "");
    EXPECT_TRUE(dlist.first() == &baz[3], "");

    dlist.remove(dlist.first());
    EXPECT_EQ(0U, dlist.size_slow(), "");
    EXPECT_TRUE(dlist.first() == nullptr, "");

    dlist.clear();
    END_TEST;
}

UNITTEST_START_TESTCASE(list_tests)
UNITTEST("Doubly linked list", doubly_linked_one_list_test)
UNITTEST_END_TESTCASE(list_tests, "listtests", "List Tests", NULL, NULL);
