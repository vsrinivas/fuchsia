// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/dwarf_abstract_child_iterator.h"

#include <gtest/gtest.h>

namespace zxdb {

namespace {

// Stand-in for llvm::DWARFDie that emulates enough of the API to be used in the
// DwarfAbstractChildIterator.
class TestDie {
 public:
  using Vector = std::vector<TestDie>;
  using iterator = typename Vector::iterator;
  using const_iterator = typename Vector::const_iterator;

  // Constructs a null DIE.
  TestDie() = default;

  // Constructs a non-null DIE. This will report the given offset to uniquely identify it.
  explicit TestDie(uint64_t offset) : is_valid_(true), offset_(offset) {}

  TestDie(const TestDie& other)
      : is_valid_(other.is_valid_), offset_(other.offset_), children_(other.children_) {
    if (other.abstract_origin_)
      abstract_origin_ = std::make_unique<TestDie>(*other.abstract_origin_);
  }

  const TestDie& operator=(const TestDie& other) {
    is_valid_ = other.is_valid_;
    offset_ = other.offset_;
    children_ = other.children_;
    abstract_origin_.reset();
    if (other.abstract_origin_)
      abstract_origin_ = std::make_unique<TestDie>(*other.abstract_origin_);
    return *this;
  }

  bool operator==(const TestDie& other) const {
    // Assume the offsets uniquely identify a DIE.
    return offset_ == other.offset_;
  }

  void set_is_null(bool iv) { is_valid_ = iv; }
  TestDie& abstract_origin() {
    // Lazily create.
    if (!abstract_origin_)
      abstract_origin_ = std::make_unique<TestDie>();
    return *abstract_origin_;
  }
  Vector& children() { return children_; }

  const_iterator begin() const { return children_.begin(); }
  const_iterator end() const { return children_.end(); }
  iterator begin() { return children_.begin(); }
  iterator end() { return children_.end(); }

  // DWARFDie implementation (enough for the iterator).
  TestDie getAttributeValueAsReferencedDie(llvm::dwarf::Attribute attr) const {
    // Only expect abstract origin queries in this test.
    EXPECT_EQ(attr, llvm::dwarf::DW_AT_abstract_origin);
    return const_cast<TestDie*>(this)->abstract_origin();
  }
  bool isValid() const { return is_valid_; }
  uint64_t getOffset() const { return offset_; }

 private:
  bool is_valid_ = false;
  uint64_t offset_ = 0;
  std::unique_ptr<TestDie> abstract_origin_;

  Vector children_;
};

using TestIterator = DwarfAbstractChildIteratorBase<TestDie>;

}  // namespace

TEST(DwarfAbstractChildIterator, Null) {
  TestDie empty;
  TestIterator iter(empty);

  auto begin = iter.begin();
  EXPECT_EQ(iter.begin(), iter.end());
}

TEST(DwarfAbstractChildIterator, Empty) {
  TestDie empty(22);  // Give some arbitrary nonzero offset for not-null DIE.
  TestIterator iter(empty);

  EXPECT_EQ(iter.begin(), iter.end());

  // Check that a range-based for loop works on the iterators and reports nothing.
  int count = 0;
  for (const auto& child : iter) {
    (void)child;
    count++;
  }
  EXPECT_EQ(0, count);
}

// Tests child iteration with no abstract origin.
TEST(DwarfAbstractChildIterator, NoAbstract) {
  TestDie root(1);

  uint64_t expected_child_offset = 100;
  root.children().emplace_back(expected_child_offset);
  root.children().emplace_back(expected_child_offset + 1);
  root.children().emplace_back(expected_child_offset + 2);

  EXPECT_EQ(102u, root.children().back().getOffset());

  // Validate the unique offsets of each child.
  for (const auto& child : TestIterator(root)) {
    EXPECT_EQ(expected_child_offset, child.getOffset());
    expected_child_offset++;
  }
  EXPECT_EQ(103u, expected_child_offset);  // Should have seen them all.
}

TEST(DwarfAbstractChildIterator, Abstract) {
  // These are the DIEs in each class, with lines indicating the shadowing:
  //
  //   CONCRETE       ABSTRACT1      ABSTRACT2
  //                                   100
  //     301 ------------------------- 101
  //                    202 ---------- 102
  //     303 ---------- 203 ---------- 103
  //     304 ---------- 204
  //                    205
  //
  // As a result, iterating should show, in order: 301, 303, 304, 202, 205, and 100.

  TestDie concrete(1);
  TestDie abstract1(2);
  TestDie abstract2(3);

  abstract2.children().emplace_back(100);  // Visible, unique.
  abstract2.children().emplace_back(101);  // Shadowed by concrete.
  abstract2.children().emplace_back(102);  // Shadowed by abstract1.
  abstract2.children().emplace_back(103);  // Shadowed by both concrete and abstract1.

  abstract1.children().emplace_back(202);  // Visible, shadows abstract2.
  abstract1.children().back().abstract_origin() = abstract2.children()[2];  // 102
  abstract1.children().emplace_back(203);  // Shadows abstract2, shadowed by concrete.
  abstract1.children().back().abstract_origin() = abstract2.children()[3];  // 103
  abstract1.children().emplace_back(204);                                   // Shadowed by concrete.
  abstract1.children().emplace_back(205);                                   // Visible, unique.

  concrete.children().emplace_back(301);                                   // Shadows abstract2.
  concrete.children().back().abstract_origin() = abstract2.children()[1];  // 101
  concrete.children().emplace_back(303);  // Shadows abstract1 and abstract2.
  concrete.children().back().abstract_origin() = abstract1.children()[1];  // 203
  concrete.children().emplace_back(304);                                   // Shadows abstract1.
  concrete.children().back().abstract_origin() = abstract1.children()[2];  // 204

  // Connect the tree.
  //
  // This will COPY the values so changes to our local vars won't reflect in the abstract origin
  // hierarchy from here down.
  abstract1.abstract_origin() = abstract2;
  concrete.abstract_origin() = abstract1;

  // Iterate through the values.
  std::vector<uint64_t> expected{301, 303, 304, 202, 205, 100};  // See comment above.
  size_t i = 0;
  for (const auto& child : TestIterator(concrete)) {
    ASSERT_LT(i, expected.size());
    EXPECT_EQ(expected[i], child.getOffset());
    ++i;
  }
  EXPECT_EQ(i, expected.size());
}

// This tests several combinations of DIEs in a hierarchy having no children.
TEST(DwarfAbstractChildIterator, ConcreteNoChildren) {
  TestDie concrete(1);
  TestDie abstract1(2);
  TestDie abstract2(3);
  TestDie abstract3(4);

  abstract2.children().emplace_back(101);
  abstract2.children().emplace_back(102);

  abstract2.abstract_origin() = abstract3;
  abstract1.abstract_origin() = abstract2;
  concrete.abstract_origin() = abstract1;

  TestIterator iter(concrete);
  auto cur = iter.begin();

  // We should find both children on the double-abstract origin.
  ASSERT_NE(cur, iter.end());
  EXPECT_EQ(cur->getOffset(), 101u);
  ++cur;
  ASSERT_NE(cur, iter.end());
  EXPECT_EQ(cur->getOffset(), 102u);
  ++cur;
  EXPECT_EQ(cur, iter.end());
}

}  // namespace zxdb
