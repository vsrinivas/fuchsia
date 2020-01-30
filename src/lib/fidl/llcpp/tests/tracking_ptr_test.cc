// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/tracking_ptr.h>

#include <set>
#include <unordered_set>

#include "gtest/gtest.h"

struct DestructionState {
  bool destructor_called = false;
};
struct DestructableObject {
  DestructableObject() : ds(nullptr) {}
  DestructableObject(DestructionState* ds) : ds(ds) {}

  ~DestructableObject() { ds->destructor_called = true; }

  DestructionState* ds;
};

TEST(TrackingPtr, DefaultConstructor) {
  fidl::tracking_ptr<int32_t> ptr;
  EXPECT_EQ(ptr, nullptr);
}

TEST(TrackingPtr, SetGet) {
  int32_t x;
  fidl::tracking_ptr<int32_t> ptr(fidl::unowned(&x));
  EXPECT_EQ(ptr.get(), &x);
}

TEST(TrackingPtr, UnownedSingleValueLifecycle) {
  DestructionState ds1, ds2;
  DestructableObject obj1(&ds1), obj2(&ds2);
  {
    fidl::tracking_ptr<DestructableObject> ptr1 = fidl::unowned_ptr<DestructableObject>(&obj1);
    fidl::tracking_ptr<DestructableObject> ptr2 = fidl::unowned_ptr<DestructableObject>(&obj2);
    ptr2 = std::move(ptr1);
  }
  EXPECT_FALSE(ds1.destructor_called);
  EXPECT_FALSE(ds2.destructor_called);
}

TEST(TrackingPtr, OwnedSingleValueLifecycle) {
  DestructionState ds1, ds2;
  {
    fidl::tracking_ptr<DestructableObject> ptr1 = std::make_unique<DestructableObject>(&ds1);
    fidl::tracking_ptr<DestructableObject> ptr2 = std::make_unique<DestructableObject>(&ds2);
    EXPECT_FALSE(ds1.destructor_called);
    EXPECT_FALSE(ds2.destructor_called);
    ptr2 = std::move(ptr1);
    EXPECT_FALSE(ds1.destructor_called);
    EXPECT_TRUE(ds2.destructor_called);
  }
  EXPECT_TRUE(ds1.destructor_called);
}

TEST(TrackingPtr, UnownedArrayLifecycle) {
  DestructionState ds1[2] = {};
  DestructionState ds2[2] = {};
  DestructableObject arr1[] = {&ds1[0], &ds1[1]};
  DestructableObject arr2[] = {&ds2[0], &ds2[1]};
  {
    fidl::tracking_ptr<DestructableObject[]> ptr1 = fidl::unowned_ptr<DestructableObject>(arr1);
    fidl::tracking_ptr<DestructableObject[]> ptr2 = fidl::unowned_ptr<DestructableObject>(arr2);
    ptr2 = std::move(ptr1);
  }
  EXPECT_FALSE(ds1[0].destructor_called);
  EXPECT_FALSE(ds1[1].destructor_called);
  EXPECT_FALSE(ds2[0].destructor_called);
  EXPECT_FALSE(ds2[1].destructor_called);
}

TEST(TrackingPtr, OwnedArrayLifecycle) {
  DestructionState ds1[2] = {};
  DestructionState ds2[2] = {};

  {
    auto arr1 = std::make_unique<DestructableObject[]>(2);
    arr1[0].ds = &ds1[0];
    arr1[1].ds = &ds1[1];
    fidl::tracking_ptr<DestructableObject[]> ptr1(std::move(arr1));
    auto arr2 = std::make_unique<DestructableObject[]>(2);
    arr2[0].ds = &ds2[0];
    arr2[1].ds = &ds2[1];
    fidl::tracking_ptr<DestructableObject[]> ptr2(std::move(arr2));
    EXPECT_FALSE(ds1[0].destructor_called);
    EXPECT_FALSE(ds1[1].destructor_called);
    EXPECT_FALSE(ds2[0].destructor_called);
    EXPECT_FALSE(ds2[1].destructor_called);
    ptr2 = std::move(ptr1);
    EXPECT_FALSE(ds1[0].destructor_called);
    EXPECT_FALSE(ds1[1].destructor_called);
    EXPECT_TRUE(ds2[0].destructor_called);
    EXPECT_TRUE(ds2[1].destructor_called);
  }
  EXPECT_TRUE(ds1[0].destructor_called);
  EXPECT_TRUE(ds1[1].destructor_called);
}

TEST(TrackingPtr, SingleValueOperatorBool) {
  fidl::tracking_ptr<int32_t> default_ptr;
  EXPECT_FALSE(default_ptr);
  int32_t val = 1;
  fidl::tracking_ptr<int32_t> ptr = fidl::unowned_ptr<int32_t>(&val);
  EXPECT_TRUE(ptr);
  ptr = nullptr;
  EXPECT_FALSE(ptr);
  ptr = 0;
  EXPECT_FALSE(ptr);
}

TEST(TrackingPtr, ArrayOperatorBool) {
  int32_t arr[3] = {};
  fidl::tracking_ptr<int32_t[]> ptr(fidl::unowned(arr));
  EXPECT_TRUE(ptr);
  ptr = nullptr;
  EXPECT_FALSE(ptr);
}

TEST(TrackingPtr, VoidOperatorBool) {
  int32_t val = 1;
  fidl::tracking_ptr<int32_t> int_ptr = fidl::unowned_ptr<int32_t>(&val);
  fidl::tracking_ptr<void> nonnull_ptr(std::move(int_ptr));
  EXPECT_TRUE(nonnull_ptr);

  fidl::tracking_ptr<void> null_ptr((fidl::tracking_ptr<int32_t>(nullptr)));
  EXPECT_FALSE(null_ptr);
}

TEST(TrackingPtr, SingleValueDereference) {
  struct TestStruct {
    int a;
  };
  TestStruct example{.a = 1};
  fidl::tracking_ptr<TestStruct> example_ptr = fidl::unowned_ptr<TestStruct>(&example);
  EXPECT_EQ((*example_ptr).a, 1);
  EXPECT_EQ(example_ptr->a, 1);
  *example_ptr = TestStruct{.a = 2};
  EXPECT_EQ(example_ptr->a, 2);
}

TEST(TrackingPtr, ArrayIndexing) {
  int32_t arr[3] = {1, 2, 3};
  fidl::tracking_ptr<int32_t[]> ptr = fidl::unowned_ptr<int32_t>(arr);
  EXPECT_EQ(ptr[1], 2);
  ptr[0] = 4;
  EXPECT_EQ(ptr[0], 4);
}

TEST(TrackingPtr, Swap) {
  int32_t x, y;
  fidl::tracking_ptr<int32_t> x_ptr = fidl::unowned_ptr<int32_t>(&x);
  fidl::tracking_ptr<int32_t> y_ptr = fidl::unowned_ptr<int32_t>(&y);
  std::swap(x_ptr, y_ptr);
  EXPECT_EQ(x_ptr.get(), &y);
  EXPECT_EQ(y_ptr.get(), &x);
}

TEST(TrackingPtr, SingleValueHashing) {
  int32_t val;
  EXPECT_EQ(std::hash<fidl::tracking_ptr<int32_t>>{}(fidl::unowned_ptr<int32_t>(&val)),
            std::hash<int32_t*>{}(&val));

  // Ensure that hashing is correctly implemented so unordered_set can be used.
  std::unordered_set<fidl::tracking_ptr<int32_t>> set;
  set.insert(fidl::unowned(&val));
}

TEST(TrackingPtr, ArrayHashing) {
  int32_t arr[3]{};
  EXPECT_EQ(std::hash<fidl::tracking_ptr<int32_t[]>>{}(fidl::unowned_ptr<int32_t>(arr)),
            std::hash<int32_t*>{}(arr));

  // Ensure that hashing is correctly implemented so unordered_set can be used.
  std::unordered_set<fidl::tracking_ptr<int32_t[]>> set;
  set.insert(fidl::unowned(arr));
}

TEST(TrackingPtr, Comparison) {
  int32_t* lower_ptr = reinterpret_cast<int32_t*>(0x10);
  int32_t* upper_ptr = reinterpret_cast<int32_t*>(0x20);
  fidl::tracking_ptr<int32_t> lower = fidl::unowned_ptr<int32_t>(lower_ptr);
  fidl::tracking_ptr<int32_t> lower2 = fidl::unowned_ptr<int32_t>(lower_ptr);
  fidl::tracking_ptr<int32_t> upper = fidl::unowned_ptr<int32_t>(upper_ptr);

  EXPECT_TRUE(lower == lower2);
  EXPECT_FALSE(lower == upper);
  EXPECT_TRUE(lower != upper);
  EXPECT_FALSE(lower != lower2);
  EXPECT_TRUE(lower < upper);
  EXPECT_FALSE(lower < lower);
  EXPECT_FALSE(upper < lower);
  EXPECT_TRUE(lower <= upper);
  EXPECT_TRUE(lower <= lower);
  EXPECT_FALSE(upper <= lower);
  EXPECT_TRUE(upper > lower);
  EXPECT_FALSE(upper > upper);
  EXPECT_FALSE(lower > upper);
  EXPECT_TRUE(upper >= lower);
  EXPECT_TRUE(upper >= upper);
  EXPECT_FALSE(lower >= upper);

  EXPECT_FALSE(lower == nullptr);
  EXPECT_FALSE(nullptr == lower);
  EXPECT_TRUE(lower != nullptr);
  EXPECT_TRUE(nullptr != lower);

  // Ensure that comparison is correctly implemented so set can be used.
  std::set<fidl::tracking_ptr<int32_t>> set;
  set.insert(fidl::unowned(lower_ptr));
}

TEST(TrackingPtr, Casting) {
  class Base {};
  class Derived : public Base {};
  Derived d;
  fidl::tracking_ptr<Derived> d_ptr = fidl::unowned_ptr<Derived>(&d);
  EXPECT_EQ(static_cast<fidl::tracking_ptr<Base>>(std::move(d_ptr)).get(), &d);

  fidl::tracking_ptr<Derived> d_ptr2 = fidl::unowned_ptr<Derived>(&d);
  auto vptr = static_cast<fidl::tracking_ptr<void>>(std::move(d_ptr2));
  EXPECT_EQ(vptr.get(), &d);

  auto d_arr_ptr = static_cast<fidl::tracking_ptr<Derived[]>>(std::move(vptr));
  EXPECT_EQ(d_arr_ptr.get(), &d);
}
