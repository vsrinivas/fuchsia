// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/intrusive_list.h"

#include "gtest/gtest.h"

namespace {
using namespace escher;

class TestObj : public IntrusiveListItem<TestObj> {
 public:
  TestObj(size_t payload) : payload_(payload) {}

  size_t payload() const { return payload_; }

 private:
  size_t payload_;
};

TEST(IntrusiveList, InsertMoveAndClear) {
  IntrusiveList<TestObj> list1, list2;
  EXPECT_TRUE(list1.IsEmpty());
  EXPECT_TRUE(list2.IsEmpty());

  TestObj obj1(1);
  TestObj obj2(2);
  TestObj obj3(3);
  EXPECT_EQ(obj1.prev, nullptr);
  EXPECT_EQ(obj1.next, nullptr);
  EXPECT_EQ(obj2.prev, nullptr);
  EXPECT_EQ(obj2.next, nullptr);
  EXPECT_EQ(obj3.prev, nullptr);
  EXPECT_EQ(obj3.next, nullptr);

  // Insert in reverse order, so that list is ordered.
  list1.InsertFront(&obj3);
  list1.InsertFront(&obj2);
  list1.InsertFront(&obj1);
  EXPECT_EQ(obj1.prev, nullptr);
  EXPECT_EQ(obj1.next, &obj2);
  EXPECT_EQ(obj2.prev, &obj1);
  EXPECT_EQ(obj2.next, &obj3);
  EXPECT_EQ(obj3.prev, &obj2);
  EXPECT_EQ(obj3.next, nullptr);

  size_t count = 0;
  for (auto& obj : list1) {
    EXPECT_NE(obj.next, obj.prev);  // avoid unused-variable warning-as-error.
    ++count;
  }
  EXPECT_EQ(count, 3U);
  EXPECT_FALSE(list1.IsEmpty());

  while (auto it = list1.begin()) {
    auto obj = it.get();
    list2.MoveToFront(list1, it);
    EXPECT_EQ(obj->prev, nullptr);
    --count;
  }
  EXPECT_EQ(count, 0U);
  EXPECT_TRUE(list1.IsEmpty());
  EXPECT_FALSE(list2.IsEmpty());
  EXPECT_EQ(obj3.prev, nullptr);
  EXPECT_EQ(obj3.next, &obj2);
  EXPECT_EQ(obj2.prev, &obj3);
  EXPECT_EQ(obj2.next, &obj1);
  EXPECT_EQ(obj1.prev, &obj2);
  EXPECT_EQ(obj1.next, nullptr);

  for (auto& obj : list2) {
    EXPECT_NE(obj.next, obj.prev);  // avoid unused-variable warning-as-error.
    ++count;
  }
  EXPECT_EQ(count, 3U);

  list2.Clear();
  count = 0;
  for (auto& obj : list2) {
    EXPECT_NE(obj.next, obj.prev);  // avoid unused-variable warning-as-error.
    ++count;
  }
  EXPECT_TRUE(list2.IsEmpty());
  EXPECT_EQ(count, 0U);
  EXPECT_EQ(obj3.next, nullptr);
  EXPECT_EQ(obj3.prev, nullptr);
  EXPECT_EQ(obj2.next, nullptr);
  EXPECT_EQ(obj2.prev, nullptr);
  EXPECT_EQ(obj1.next, nullptr);
  EXPECT_EQ(obj1.prev, nullptr);
}

TEST(IntrusiveList, Iteration) {
  IntrusiveList<TestObj> list;
  IntrusiveList<TestObj>::Iterator it;
  size_t count = 0;

  TestObj obj1(1);
  TestObj obj2(2);
  TestObj obj3(3);
  TestObj obj4(4);

  // Insert in reverse order, so that list is ordered.
  list.InsertFront(&obj4);
  list.InsertFront(&obj3);
  list.InsertFront(&obj2);
  list.InsertFront(&obj1);

  // Test post-increment.
  count = 0;
  it = list.begin();
  while (it && it++) {
    ++count;
  }
  EXPECT_EQ(count, 4U);

  // Test pre-increment.
  count = 0;
  it = list.begin();
  while (++it) {
    ++count;
  }
  EXPECT_EQ(count, 3U);

  // Test range-for.
  count = 0;
  for (auto& obj : list) {
    EXPECT_NE(obj.next, obj.prev);  // avoid unused-variable warning-as-error.
    ++count;
  }
  EXPECT_EQ(count, 4U);

  // Due to destruction order the TestObj instances die first.  Clear the list
  // now, otherwise there would be a use-after-free in the TestObj destructor.
  list.Clear();
}

TEST(IntrusiveList, Erase) {
  IntrusiveList<TestObj> list;

  TestObj obj1(1);
  TestObj obj2(2);
  TestObj obj3(3);
  TestObj obj4(4);

  // Insert in reverse order, so that list is ordered.
  list.InsertFront(&obj4);
  list.InsertFront(&obj3);
  list.InsertFront(&obj2);
  list.InsertFront(&obj1);
  EXPECT_EQ(obj1.prev, nullptr);
  EXPECT_EQ(obj1.next, &obj2);
  EXPECT_EQ(obj2.prev, &obj1);
  EXPECT_EQ(obj2.next, &obj3);
  EXPECT_EQ(obj3.prev, &obj2);
  EXPECT_EQ(obj3.next, &obj4);
  EXPECT_EQ(obj4.prev, &obj3);
  EXPECT_EQ(obj4.next, nullptr);

  auto it = list.begin();
  ++it;
  EXPECT_EQ(it.get(), &obj2);
  it = list.Erase(it);
  EXPECT_EQ(obj1.next, &obj3);
  EXPECT_EQ(obj2.next, nullptr);
  EXPECT_EQ(obj2.prev, nullptr);
  EXPECT_EQ(obj3.prev, &obj1);

  EXPECT_EQ(it.get(), &obj3);
  it = list.Erase(it);
  EXPECT_EQ(it.get(), &obj4);
  EXPECT_EQ(obj1.next, &obj4);
  EXPECT_EQ(obj3.next, nullptr);
  EXPECT_EQ(obj3.prev, nullptr);
  EXPECT_EQ(obj4.prev, &obj1);

  TestObj* obj = list.PopFront();
  EXPECT_EQ(obj, &obj1);
  obj = list.PopFront();
  EXPECT_TRUE(list.IsEmpty());
  EXPECT_EQ(obj, &obj4);
  EXPECT_EQ(obj1.next, nullptr);
  EXPECT_EQ(obj4.prev, nullptr);
  obj = list.PopFront();
  EXPECT_EQ(obj, nullptr);
}

}  // namespace
