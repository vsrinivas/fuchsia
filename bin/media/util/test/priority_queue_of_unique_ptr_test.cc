// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/util/priority_queue_of_unique_ptr.h"

#include "gtest/gtest.h"

namespace media {
namespace {

struct Element {
  static size_t destroyed_count_;
  static size_t last_destroyed_label_;

  Element(size_t label) : label_(label) {}

  ~Element() {
    ++destroyed_count_;
    last_destroyed_label_ = label_;
  }

  bool operator<(const Element& other) { return label_ < other.label_; }

  size_t label_;
};

// static
size_t Element::destroyed_count_ = 0;
// static
size_t Element::last_destroyed_label_ = 0;

// Tests whether a newly-initialized queue responds to its methods as expected.
TEST(PriorityQueueOfUniqePtrTest, InitialState) {
  priority_queue_of_unique_ptr<Element> under_test;

  EXPECT_TRUE(under_test.empty());
  EXPECT_EQ(0u, under_test.size());
}

// Tests whether a queue destroys its elements when destroyed.
TEST(PriorityQueueOfUniqePtrTest, ElementDestruction) {
  Element::destroyed_count_ = 0;

  {
    priority_queue_of_unique_ptr<Element> under_test;

    under_test.push(std::make_unique<Element>(1));
    under_test.push(std::make_unique<Element>(2));
    under_test.push(std::make_unique<Element>(3));
  }

  EXPECT_EQ(3u, Element::destroyed_count_);
}

// Tests assignment.
TEST(PriorityQueueOfUniqePtrTest, Assignment) {
  priority_queue_of_unique_ptr<Element> under_test_1;
  priority_queue_of_unique_ptr<Element> under_test_2;

  under_test_1.push(std::make_unique<Element>(1));
  under_test_1.push(std::make_unique<Element>(2));
  under_test_1.push(std::make_unique<Element>(3));

  EXPECT_EQ(3u, under_test_1.size());
  EXPECT_EQ(0u, under_test_2.size());

  under_test_2 = std::move(under_test_1);

  EXPECT_EQ(0u, under_test_1.size());
  EXPECT_EQ(3u, under_test_2.size());

  EXPECT_EQ(3u, under_test_2.top().label_);
  under_test_2.pop();
  EXPECT_EQ(2u, under_test_2.top().label_);
  under_test_2.pop();
  EXPECT_EQ(1u, under_test_2.top().label_);
  under_test_2.pop();
  EXPECT_EQ(0u, under_test_2.size());
}

// Tests the empty method.
TEST(PriorityQueueOfUniqePtrTest, Empty) {
  priority_queue_of_unique_ptr<Element> under_test;

  EXPECT_TRUE(under_test.empty());

  under_test.push(std::make_unique<Element>(1));
  EXPECT_FALSE(under_test.empty());

  under_test.push(std::make_unique<Element>(2));
  EXPECT_FALSE(under_test.empty());

  under_test.push(std::make_unique<Element>(3));
  EXPECT_FALSE(under_test.empty());

  under_test.pop();
  EXPECT_FALSE(under_test.empty());

  under_test.pop();
  EXPECT_FALSE(under_test.empty());

  under_test.pop();
  EXPECT_TRUE(under_test.empty());
}

// Tests the size method.
TEST(PriorityQueueOfUniqePtrTest, Size) {
  priority_queue_of_unique_ptr<Element> under_test;

  EXPECT_EQ(0u, under_test.size());

  under_test.push(std::make_unique<Element>(1));
  EXPECT_EQ(1u, under_test.size());

  under_test.push(std::make_unique<Element>(2));
  EXPECT_EQ(2u, under_test.size());

  under_test.push(std::make_unique<Element>(3));
  EXPECT_EQ(3u, under_test.size());

  under_test.pop();
  EXPECT_EQ(2u, under_test.size());

  under_test.pop();
  EXPECT_EQ(1u, under_test.size());

  under_test.pop();
  EXPECT_EQ(0u, under_test.size());
}

// Tests the top method (and push and pop).
TEST(PriorityQueueOfUniqePtrTest, Top) {
  Element::destroyed_count_ = 0;

  priority_queue_of_unique_ptr<Element> under_test;

  under_test.push(std::make_unique<Element>(1));
  EXPECT_EQ(1u, under_test.top().label_);

  under_test.push(std::make_unique<Element>(2));
  EXPECT_EQ(2u, under_test.top().label_);

  under_test.push(std::make_unique<Element>(3));
  EXPECT_EQ(3u, under_test.top().label_);

  EXPECT_EQ(0u, Element::destroyed_count_);

  under_test.pop();
  EXPECT_EQ(2u, under_test.top().label_);
  EXPECT_EQ(1u, Element::destroyed_count_);
  EXPECT_EQ(3u, Element::last_destroyed_label_);

  under_test.pop();
  EXPECT_EQ(1u, under_test.top().label_);
  EXPECT_EQ(2u, Element::destroyed_count_);
  EXPECT_EQ(2u, Element::last_destroyed_label_);

  under_test.pop();
  EXPECT_EQ(3u, Element::destroyed_count_);
  EXPECT_EQ(1u, Element::last_destroyed_label_);
}

// Tests the get_top method (and push and pop).
TEST(PriorityQueueOfUniqePtrTest, GetTop) {
  Element::destroyed_count_ = 0;

  priority_queue_of_unique_ptr<Element> under_test;

  under_test.push(std::make_unique<Element>(1));
  EXPECT_EQ(1u, under_test.get_top()->label_);
  EXPECT_EQ(1u, under_test.top().label_);

  under_test.push(std::make_unique<Element>(2));
  EXPECT_EQ(2u, under_test.get_top()->label_);

  under_test.push(std::make_unique<Element>(3));
  EXPECT_EQ(3u, under_test.get_top()->label_);

  EXPECT_EQ(0u, Element::destroyed_count_);

  under_test.pop();
  EXPECT_EQ(2u, under_test.get_top()->label_);
  EXPECT_EQ(1u, Element::destroyed_count_);
  EXPECT_EQ(3u, Element::last_destroyed_label_);

  under_test.pop();
  EXPECT_EQ(1u, under_test.get_top()->label_);
  EXPECT_EQ(2u, Element::destroyed_count_);
  EXPECT_EQ(2u, Element::last_destroyed_label_);

  under_test.pop();
  EXPECT_EQ(3u, Element::destroyed_count_);
  EXPECT_EQ(1u, Element::last_destroyed_label_);
}

// Tests the pop_and_move method.
TEST(PriorityQueueOfUniqePtrTest, PopAndMove) {
  Element::destroyed_count_ = 0;

  priority_queue_of_unique_ptr<Element> under_test;

  under_test.push(std::make_unique<Element>(1));
  under_test.push(std::make_unique<Element>(3));
  under_test.push(std::make_unique<Element>(2));

  std::unique_ptr<Element> element = under_test.pop_and_move();
  EXPECT_NE(nullptr, element);
  EXPECT_EQ(3u, element->label_);
  EXPECT_EQ(0u, Element::destroyed_count_);
  element.reset();
  EXPECT_EQ(1u, Element::destroyed_count_);
  EXPECT_EQ(3u, Element::last_destroyed_label_);

  element = under_test.pop_and_move();
  EXPECT_NE(nullptr, element);
  EXPECT_EQ(2u, element->label_);
  EXPECT_EQ(1u, Element::destroyed_count_);
  element.reset();
  EXPECT_EQ(2u, Element::destroyed_count_);
  EXPECT_EQ(2u, Element::last_destroyed_label_);

  element = under_test.pop_and_move();
  EXPECT_NE(nullptr, element);
  EXPECT_EQ(1u, element->label_);
  EXPECT_EQ(2u, Element::destroyed_count_);
  element.reset();
  EXPECT_EQ(3u, Element::destroyed_count_);
  EXPECT_EQ(1u, Element::last_destroyed_label_);
}

// Tests the swap method.
TEST(PriorityQueueOfUniqePtrTest, Swap) {
  Element::destroyed_count_ = 0;

  priority_queue_of_unique_ptr<Element> under_test_1;
  priority_queue_of_unique_ptr<Element> under_test_2;

  under_test_1.push(std::make_unique<Element>(1));
  under_test_1.push(std::make_unique<Element>(2));
  under_test_1.push(std::make_unique<Element>(3));

  under_test_2.push(std::make_unique<Element>(4));
  under_test_2.push(std::make_unique<Element>(5));
  under_test_2.push(std::make_unique<Element>(6));
  under_test_2.push(std::make_unique<Element>(7));

  EXPECT_EQ(3u, under_test_1.size());
  EXPECT_EQ(4u, under_test_2.size());

  under_test_1.swap(under_test_2);

  EXPECT_EQ(0u, Element::destroyed_count_);

  EXPECT_EQ(4u, under_test_1.size());
  EXPECT_EQ(3u, under_test_2.size());

  EXPECT_EQ(3u, under_test_2.top().label_);
  under_test_2.pop();
  EXPECT_EQ(2u, under_test_2.top().label_);
  under_test_2.pop();
  EXPECT_EQ(1u, under_test_2.top().label_);
  under_test_2.pop();
  EXPECT_EQ(0u, under_test_2.size());

  EXPECT_EQ(7u, under_test_1.top().label_);
  under_test_1.pop();
  EXPECT_EQ(6u, under_test_1.top().label_);
  under_test_1.pop();
  EXPECT_EQ(5u, under_test_1.top().label_);
  under_test_1.pop();
  EXPECT_EQ(4u, under_test_1.top().label_);
  under_test_1.pop();
  EXPECT_EQ(0u, under_test_1.size());
}

}  // namespace
}  // namespace media
