// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/intrusive_list.h"

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <list>
#include <ostream>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

// GoogleTest Matcher implementation to make intrusive_list<> comparison easier.
MATCHER_P(ListContains, values, "") {
  arg.intrusive_list_validate();
  return std::equal(arg.begin(), arg.end(), values.begin(), values.end());
}

namespace wlan {
namespace brcmfmac {

// Implementation of intrusive_list_validate() which validates the link pointers.
template <typename T, typename TagType>
void intrusive_list<T, TagType>::intrusive_list_validate() const noexcept {
  const link_type* link = &link_;
  do {
    EXPECT_EQ(link, link->next_->prev_);
    link = link->next_;
  } while (link != &link_);
}

namespace {

// Tag types for multiple lists.
struct DivisibleBy1Tag {};
struct DivisibleBy2Tag {};
struct DivisibleBy3Tag {};
struct DivisibleBy4Tag {};
struct DivisibleBy5Tag {};
struct DivisibleBy6Tag {};

// A simple listable integer type listable in each of the above list types.
struct Integer : public intrusive_listable<>,
                 private intrusive_listable<DivisibleBy1Tag>,
                 private intrusive_listable<DivisibleBy2Tag>,
                 private intrusive_listable<DivisibleBy3Tag>,
                 private intrusive_listable<DivisibleBy4Tag>,
                 private intrusive_listable<DivisibleBy5Tag>,
                 private intrusive_listable<DivisibleBy6Tag> {
  friend class intrusive_list<Integer, DivisibleBy1Tag>;
  friend class intrusive_list<Integer, DivisibleBy2Tag>;
  friend class intrusive_list<Integer, DivisibleBy3Tag>;
  friend class intrusive_list<Integer, DivisibleBy4Tag>;
  friend class intrusive_list<Integer, DivisibleBy5Tag>;
  friend class intrusive_list<Integer, DivisibleBy6Tag>;
  explicit Integer(int value) : value_(value) {}

  constexpr bool operator==(const Integer& other) const { return value_ == other.value_; }
  constexpr bool operator==(int i) const { return value_ == i; }
  friend std::ostream& operator<<(std::ostream& out, const Integer& i) { return (out << i.value_); }

  int value_ = 0;
};

// Make a list.
template <typename T>
intrusive_list<Integer> MakeList(T* elements) {
  intrusive_list<Integer> list;
  for (auto& e : *elements) {
    list.push_back(e);
  }
  return list;
}

// Copy a list's Integer values, since GoogleMock needs to be able to store a copy.
template <typename T>
std::list<int> MakeCopy(const T& elements) {
  std::list<int> ret;
  for (const Integer& i : elements) {
    ret.push_back(i.value_);
  }
  return ret;
}

// Test the list iterators.
TEST(IntrusiveListTest, Iterators) {
  constexpr size_t kElementCount = 1024;
  std::list<Integer> values;
  for (size_t i = 0; i < kElementCount; ++i) {
    values.emplace_back(i);
  }
  auto list = MakeList(&values);

  list.intrusive_list_validate();
  EXPECT_TRUE(std::equal(list.begin(), list.end(), values.begin(), values.end()));
  EXPECT_TRUE(std::equal(list.cbegin(), list.cend(), values.cbegin(), values.cend()));
  EXPECT_TRUE(std::equal(list.rbegin(), list.rend(), values.rbegin(), values.rend()));
  EXPECT_TRUE(std::equal(list.crbegin(), list.crend(), values.crbegin(), values.crend()));
};

// Test list modification.
TEST(IntrusiveListTest, ListModification) {
  std::list<Integer> values;
  intrusive_list<Integer> list;

  // Test the front and back accessors/modifiers.
  EXPECT_TRUE(list.empty());
  EXPECT_THAT(list, ListContains(MakeCopy(values)));
  values.emplace_back(1);
  list.push_back(values.back());
  EXPECT_FALSE(list.empty());
  EXPECT_THAT(list, ListContains(MakeCopy(values)));
  values.emplace_back(2);
  list.push_back(values.back());
  EXPECT_THAT(list, ListContains(MakeCopy(values)));
  values.emplace_front(3);
  list.push_front(values.front());
  EXPECT_THAT(list, ListContains(MakeCopy(values)));
  values.emplace_back(4);
  list.push_back(values.back());
  EXPECT_THAT(list, ListContains(MakeCopy(values)));
  values.emplace_front(5);
  list.push_front(values.front());
  EXPECT_THAT(list, ListContains(MakeCopy(values)));

  // Deleting a value should auto-remove it from the list.
  EXPECT_THAT(list, ListContains(std::initializer_list<int>{5, 3, 1, 2, 4}));
  values.erase(++values.begin());
  EXPECT_THAT(list, ListContains(MakeCopy(values)));
  EXPECT_THAT(list, ListContains(std::initializer_list<int>{5, 1, 2, 4}));
  EXPECT_FALSE(list.empty());
  EXPECT_EQ(4u, list.size());

  // Erase elements and validate.
  EXPECT_FALSE(static_cast<intrusive_listable<void>&>(*std::prev(values.end(), 2)).empty());
  EXPECT_EQ(4, (*list.erase(std::prev(list.end(), 2))).value_);
  EXPECT_TRUE(static_cast<intrusive_listable<void>&>(*std::prev(values.end(), 2)).empty());
  EXPECT_THAT(list, ListContains(std::initializer_list<int>{5, 1, 4}));
  EXPECT_FALSE(list.empty());
  EXPECT_EQ(3u, list.size());
  list.pop_front();
  EXPECT_THAT(list, ListContains(std::initializer_list<int>{1, 4}));

  // Insert an element in the middle.
  Integer i(10);
  EXPECT_TRUE(static_cast<intrusive_listable<void>&>(i).empty());
  EXPECT_EQ(10, (*list.insert(std::next(list.begin()), i)).value_);
  EXPECT_FALSE(static_cast<intrusive_listable<void>&>(i).empty());
  EXPECT_THAT(list, ListContains(std::initializer_list<int>{1, 10, 4}));

  // Now continue popping until empty.
  list.pop_back();
  EXPECT_THAT(list, ListContains(std::initializer_list<int>{1, 10}));
  list.pop_front();
  EXPECT_THAT(list, ListContains(std::initializer_list<int>{10}));
  list.pop_back();
  EXPECT_THAT(list, ListContains(std::initializer_list<int>{}));
  EXPECT_TRUE(list.empty());
  EXPECT_EQ(0u, list.size());
}

// Test list modification, multiple elements.
TEST(IntrusiveListTest, ListMultiModification) {
  std::list<Integer> values_1;
  values_1.emplace_back(2);
  values_1.emplace_back(4);
  values_1.emplace_back(6);
  values_1.emplace_back(8);
  std::list<Integer> values_2;
  values_2.emplace_back(1);
  std::list<Integer> values_3;
  values_3.emplace_back(3);
  values_3.emplace_back(5);
  std::list<Integer> values_4;
  values_4.emplace_back(7);
  auto list_1 = MakeList(&values_1);
  auto list_2 = MakeList(&values_2);
  auto list_3 = MakeList(&values_3);
  auto list_4 = MakeList(&values_4);
  intrusive_list<Integer> list_5;

  // Do some splicing of entire lists, including an empty list.
  list_1.splice(list_1.begin(), std::move(list_2));
  list_1.splice(list_1.end(), std::move(list_4));
  list_1.splice(std::next(list_1.begin(), 2), std::move(list_3));
  list_1.splice(list_1.end(), std::move(list_5));
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{1, 2, 3, 5, 4, 6, 8, 7}));
  EXPECT_TRUE(list_2.empty());
  EXPECT_TRUE(list_3.empty());
  EXPECT_TRUE(list_4.empty());
  EXPECT_TRUE(list_5.empty());

  // Move around a bit.
  Integer single_item(10);
  list_2.push_back(single_item);
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{1, 2, 3, 5, 4, 6, 8, 7}));
  EXPECT_THAT(list_2, ListContains(std::initializer_list<int>{10}));
  EXPECT_FALSE(static_cast<intrusive_listable<void>&>(single_item).empty());
  list_2 = std::move(list_1);
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{}));
  EXPECT_THAT(list_2, ListContains(std::initializer_list<int>{1, 2, 3, 5, 4, 6, 8, 7}));
  EXPECT_TRUE(static_cast<intrusive_listable<void>&>(single_item).empty());
  list_1 = std::move(list_2);
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{1, 2, 3, 5, 4, 6, 8, 7}));
  EXPECT_THAT(list_2, ListContains(std::initializer_list<int>{}));

  // Swap around a bit.
  using std::swap;
  swap(list_1, list_2);
  EXPECT_TRUE(list_1.empty());
  EXPECT_THAT(list_2, ListContains(std::initializer_list<int>{1, 2, 3, 5, 4, 6, 8, 7}));
  Integer i1(42);
  Integer i2(43);
  list_1.push_back(i1);
  list_1.push_back(i2);
  swap(list_1, list_2);
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{1, 2, 3, 5, 4, 6, 8, 7}));
  EXPECT_THAT(list_2, ListContains(std::initializer_list<int>{42, 43}));

  // Re-adding an element to another list will remove it from the existing list.
  list_3.push_back(i2);
  EXPECT_THAT(list_2, ListContains(std::initializer_list<int>{42}));
  EXPECT_THAT(list_3, ListContains(std::initializer_list<int>{43}));

  // Erase multiple elements from the middle, and then front of the list.
  EXPECT_EQ(8, (*list_1.erase(std::next(list_1.begin(), 3), std::next(list_1.begin(), 6))).value_);
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{1, 2, 3, 8, 7}));
  EXPECT_EQ(3, (*list_1.erase(list_1.begin(), std::next(list_1.begin(), 2))).value_);
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{3, 8, 7}));

  // Add a few elements back into the middle.
  std::list<Integer> values;
  values.emplace_back(11);
  values.emplace_back(12);
  EXPECT_EQ(11, (*list_1.insert(std::prev(list_1.end(), 2), values.begin(), values.end())).value_);
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{3, 11, 12, 8, 7}));

  // Now remove elements from the back of the list, and then clear it out.
  EXPECT_EQ(list_1.end(), list_1.erase(std::prev(list_1.end(), 3), list_1.end()));
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{3, 11}));
  list_1.clear();
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{}));
  list_1.clear();
  EXPECT_THAT(list_1, ListContains(std::initializer_list<int>{}));
}

// Test that we can add a intrusive_listable<> to different intrusive_list<> instances by tag.
TEST(IntrusiveListTest, AddToMultipleLists) {
  constexpr int kIntegerCount = 1024;
  std::list<Integer> integers;
  for (size_t i = 0; i < kIntegerCount; ++i) {
    integers.emplace_back(i);
  }

  intrusive_list<Integer, DivisibleBy1Tag> list_1;
  intrusive_list<Integer, DivisibleBy2Tag> list_2;
  intrusive_list<Integer, DivisibleBy3Tag> list_3;
  intrusive_list<Integer, DivisibleBy4Tag> list_4;
  intrusive_list<Integer, DivisibleBy5Tag> list_5;
  intrusive_list<Integer, DivisibleBy6Tag> list_6;

  for (auto& integer : integers) {
    if ((integer.value_ % 1) == 0) {
      list_1.push_back(integer);
    }
    if ((integer.value_ % 2) == 0) {
      list_2.push_back(integer);
    }
    if ((integer.value_ % 3) == 0) {
      list_3.push_back(integer);
    }
    if ((integer.value_ % 4) == 0) {
      list_4.push_back(integer);
    }
    if ((integer.value_ % 5) == 0) {
      list_5.push_back(integer);
    }
    if ((integer.value_ % 6) == 0) {
      list_6.push_back(integer);
    }
  }

  {
    std::list<int> values((kIntegerCount + 0) / 1);
    std::generate(values.begin(), values.end(), [n = 0]() mutable {
      const int v = n;
      n += 1;
      return v;
    });
    EXPECT_THAT(list_1, ListContains(values));
  }
  {
    std::list<int> values((kIntegerCount + 1) / 2);
    std::generate(values.begin(), values.end(), [n = 0]() mutable {
      const int v = n;
      n += 2;
      return v;
    });
    EXPECT_THAT(list_2, ListContains(values));
  }
  {
    std::list<int> values((kIntegerCount + 2) / 3);
    std::generate(values.begin(), values.end(), [n = 0]() mutable {
      const int v = n;
      n += 3;
      return v;
    });
    EXPECT_THAT(list_3, ListContains(values));
  }
  {
    std::list<int> values((kIntegerCount + 3) / 4);
    std::generate(values.begin(), values.end(), [n = 0]() mutable {
      const int v = n;
      n += 4;
      return v;
    });
    EXPECT_THAT(list_4, ListContains(values));
  }
  {
    std::list<int> values((kIntegerCount + 4) / 5);
    std::generate(values.begin(), values.end(), [n = 0]() mutable {
      const int v = n;
      n += 5;
      return v;
    });
    EXPECT_THAT(list_5, ListContains(values));
  }
  {
    std::list<int> values((kIntegerCount + 5) / 6);
    std::generate(values.begin(), values.end(), [n = 0]() mutable {
      const int v = n;
      n += 6;
      return v;
    });
    EXPECT_THAT(list_6, ListContains(values));
  }

  integers.clear();
  EXPECT_TRUE(list_1.empty());
  EXPECT_TRUE(list_2.empty());
  EXPECT_TRUE(list_3.empty());
  EXPECT_TRUE(list_4.empty());
  EXPECT_TRUE(list_5.empty());
  EXPECT_TRUE(list_6.empty());
}

}  // namespace
}  // namespace brcmfmac
}  // namespace wlan
