// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
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
    Foo(int val) : TestValue(val), next_(nullptr) {}

    void list_set_next(Foo* foo) { next_ = foo; }
    const Foo* list_next() const { return next_; }
    Foo* list_next() { return next_; }

private:
    Foo* next_;
};

class Bar : public TestValue {
public:
    Bar(int val) : TestValue(val), next_l1_(nullptr), next_l2_(nullptr) {}

    struct List1Traits {
        static Bar* next(Bar* bar) { return bar->next_l1_; }
        static const Bar* next(const Bar* bar) { return bar->next_l1_; }
        static void set_next(Bar* bar, Bar* next) { bar->next_l1_ = next; }
    };

    struct List2Traits {
        static Bar* next(Bar* bar) { return bar->next_l2_; }
        static const Bar* next(const Bar* bar) { return bar->next_l2_; }
        static void set_next(Bar* bar, Bar* next) { bar->next_l2_ = next; }
    };

private:
    Bar* next_l1_;
    Bar* next_l2_;
};

class Baz : public TestValue {
public:
    Baz(int val) : TestValue(val), next_(nullptr), prev_(nullptr) {}

    Baz* list_prev() { return prev_; }
    const Baz* list_prev() const { return prev_; }

    Baz* list_next() { return next_; }
    const Baz* list_next() const { return next_; }

    void list_set_prev(Baz* prev) { prev_ = prev; }
    void list_set_next(Baz* next) { next_ = next; }

private:
    Baz* next_;
    Baz* prev_;
};


static bool singly_linked_one_list_test(void* context) {
    BEGIN_TEST;
    const size_t count = 7;
    int prev = 0;

    Foo foo[count] = {1, 2, 3, 4, 5, 6, 7};

    utils::SinglyLinkedList<Foo> slist;

    EXPECT_EQ(0U, slist.size_slow(), "");

    for (int ix = 0; ix != count; ++ix) {
        slist.push_front(&foo[ix]);
    }

    EXPECT_EQ(count, slist.size_slow(), "");

    Foo* found = utils::find_if(&slist, [](Foo* f) {
      return f->value() == 4;
    });

    EXPECT_TRUE(found == &foo[3], "");

    int sum = 0;
    utils::for_each(&slist, [&sum](Foo* f) { sum += f->value(); });

    EXPECT_EQ(28, sum, "");

    prev = 8;
    for (int ix = 0; ix != count; ++ix) {
        Foo* foo = slist.pop_front();
        EXPECT_TRUE(prev > foo->value(), "");
        prev = foo->value();
    }

    EXPECT_TRUE(slist.is_empty(), "");

    slist.push_front(&foo[2]);
    slist.push_front(&foo[3]);
    slist.push_front(&foo[4]);

    found = utils::pop_if(&slist, [](Foo* f) {
      return f->value() == 4;
    });

    EXPECT_EQ(2U, slist.size_slow(), "");

    found = utils::pop_if(&slist, [](Foo* f) {
      return f->value() == 3;
    });

    EXPECT_EQ(1U, slist.size_slow(), "");

    slist.clear();
    END_TEST;
}

static bool singly_linked_two_lists_test(void* context) {
    BEGIN_TEST;
    const size_t count = 7;
    int prev = 0;

    Bar bar[count] = {7, 6, 5, 4, 3, 2, 1};

    utils::SinglyLinkedList<Bar, Bar::List1Traits> slist1;
    utils::SinglyLinkedList<Bar, Bar::List2Traits> slist2;

    for (int ix = 0; ix != count; ++ix) {
        slist1.push_front(&bar[ix]);
        slist2.push_front(&bar[ix]);
    }

    EXPECT_EQ(count, slist1.size_slow(), "");
    EXPECT_EQ(count, slist2.size_slow(), "");

    prev = 0;
    for (int ix = 0; ix != count; ++ix) {
        Bar* bar = slist1.pop_front();
        EXPECT_TRUE(prev < bar->value(), "");
        prev = bar->value();
    }

    prev = 0;
    for (int ix = 0; ix != count; ++ix) {
        Bar* bar = slist2.pop_front();
        EXPECT_TRUE(prev < bar->value(), "");
        prev = bar->value();
    }

    EXPECT_TRUE(slist1.is_empty(), "");
    EXPECT_TRUE(slist2.is_empty(), "");
    END_TEST;
}

static bool doubly_linked_one_list_test(void* context) {
    BEGIN_TEST;
    const size_t count = 5;

    Baz baz[count] = {1, 2, 3, 4, 5};

    utils::DoublyLinkedList<Baz> dlist;

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
    for_each(&dlist, [&sum](Baz* b) { sum += b->value(); });
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

static bool list_move_test(void* context)
{
    BEGIN_TEST;
    const size_t count = 7;
    Foo foo[count] = {1, 2, 3, 4, 5, 6, 7};
    utils::SinglyLinkedList<Foo> slist_src;

    for (int ix = 0; ix != count; ++ix) {
        slist_src.push_front(&foo[ix]);
    }

    utils::SinglyLinkedList<Foo> slist_dst;
    utils::move_if(&slist_src, &slist_dst, [](Foo* foo) {
        return ((foo->value() % 2) == 0);
    });

    EXPECT_EQ(3U, slist_dst.size_slow(), "");
    EXPECT_EQ(4U, slist_src.size_slow(), "");

    utils::move_if(&slist_dst, &slist_src, [](Foo* foo) {
        return ((foo->value() % 2) == 0);
    });

    EXPECT_EQ(0U, slist_dst.size_slow(), "");
    EXPECT_EQ(7U, slist_src.size_slow(), "");

    slist_src.clear();
    END_TEST;
}

UNITTEST_START_TESTCASE(list_tests)
UNITTEST("Singly one linked list", singly_linked_one_list_test)
UNITTEST("Single two linked lists", singly_linked_two_lists_test)
UNITTEST("Doubly linked list", doubly_linked_one_list_test)
UNITTEST("Move list", list_move_test)
UNITTEST_END_TESTCASE(list_tests, "listtests", "List Tests", NULL, NULL);
