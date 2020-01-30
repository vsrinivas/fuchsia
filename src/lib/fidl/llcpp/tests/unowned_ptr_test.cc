// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/unowned_ptr.h>

#include <unordered_set>

#include "gtest/gtest.h"

TEST(UnownedPtr, Constructor) {
  int32_t val = 1;
  fidl::unowned_ptr<int32_t> a(&val);
  EXPECT_EQ(a.get(), &val);
  fidl::unowned_ptr<int32_t> b(a);
  EXPECT_EQ(a.get(), &val);
  EXPECT_EQ(b.get(), &val);
  fidl::unowned_ptr<int32_t> c(nullptr);
  EXPECT_EQ(c.get(), nullptr);
}

TEST(UnownedPtr, VoidConstructor) {
  int32_t val = 1;
  void* vptr = &val;
  fidl::unowned_ptr<void> a(vptr);
  EXPECT_EQ(a.get(), vptr);
  fidl::unowned_ptr<void> b(a);
  EXPECT_EQ(a.get(), vptr);
  EXPECT_EQ(b.get(), vptr);
  fidl::unowned_ptr<void> c(nullptr);
  EXPECT_EQ(c.get(), nullptr);
}

TEST(UnownedPtr, Destructor) {
  struct DestructableObject {
    ~DestructableObject() { *destructor_called = true; }
    bool* destructor_called;
  };

  bool destructor_called = false;
  DestructableObject x{.destructor_called = &destructor_called};
  { fidl::unowned_ptr<DestructableObject> ptr(&x); }
  EXPECT_FALSE(destructor_called);
}

TEST(UnownedPtr, Assignment) {
  int32_t val1 = 1, val2 = 2;
  fidl::unowned_ptr<int32_t> a(&val1);
  EXPECT_EQ(a.get(), &val1);
  fidl::unowned_ptr<int32_t> b(&val2);
  EXPECT_EQ(b.get(), &val2);
  b = a;
  EXPECT_EQ(a.get(), &val1);
  EXPECT_EQ(b.get(), &val1);
  b = std::move(a);
  EXPECT_EQ(a.get(), &val1);
  EXPECT_EQ(b.get(), &val1);
  b = &val2;
  EXPECT_EQ(b.get(), &val2);
  b = nullptr;
  EXPECT_EQ(b.get(), nullptr);
}

TEST(UnownedPtr, VoidAssignment) {
  int32_t val1 = 1, val2 = 2;
  void* vptr1 = &val1;
  void* vptr2 = &val2;
  fidl::unowned_ptr<void> a(vptr1);
  EXPECT_EQ(a.get(), vptr1);
  fidl::unowned_ptr<void> b(vptr2);
  EXPECT_EQ(b.get(), vptr2);
  b = a;
  EXPECT_EQ(a.get(), vptr1);
  EXPECT_EQ(b.get(), vptr1);
  b = vptr2;
  EXPECT_EQ(b.get(), vptr2);
  b = nullptr;
  EXPECT_EQ(b.get(), nullptr);
}

TEST(UnownedPtr, OperatorBool) {
  fidl::unowned_ptr<int32_t> default_ptr;
  EXPECT_FALSE(default_ptr);
  int32_t val = 1;
  fidl::unowned_ptr<int32_t> ptr(&val);
  EXPECT_TRUE(ptr);
  ptr = nullptr;
  EXPECT_FALSE(ptr);
  ptr = reinterpret_cast<int32_t*>(0);
  EXPECT_FALSE(ptr);
}

TEST(UnownedPtr, Dereference) {
  struct TestStruct {
    int a;
  };
  TestStruct example{.a = 1};
  fidl::unowned_ptr<TestStruct> example_ptr(&example);
  EXPECT_EQ((*example_ptr).a, 1);
  EXPECT_EQ(example_ptr->a, 1);
  *example_ptr = TestStruct{.a = 2};
  EXPECT_EQ(example_ptr->a, 2);
}

TEST(UnownedPtr, Indexing) {
  int32_t arr[3] = {1, 2, 3};
  fidl::unowned_ptr<int32_t> ptr(arr);
  EXPECT_EQ(ptr[1], 2);
  ptr[0] = 4;
  EXPECT_EQ(ptr[0], 4);
}

TEST(UnownedPtr, Swap) {
  int32_t x, y;
  fidl::unowned_ptr<int32_t> x_ptr(&x);
  fidl::unowned_ptr<int32_t> y_ptr(&y);
  std::swap(x_ptr, y_ptr);
  EXPECT_EQ(x_ptr.get(), &y);
  EXPECT_EQ(y_ptr.get(), &x);
}

TEST(UnownedPtr, Hashing) {
  int32_t val;
  fidl::unowned_ptr<int32_t> ptr(&val);
  EXPECT_EQ(std::hash<fidl::unowned_ptr<int32_t>>{}(ptr), std::hash<int32_t*>{}(&val));

  // Ensure that hashing is correctly implemented so unordered_set can be used.
  std::unordered_set<fidl::unowned_ptr<int32_t>> set;
  set.insert(ptr);
}

TEST(UnownedPtr, Comparison) {
  int32_t* lower_ptr = reinterpret_cast<int32_t*>(1);
  int32_t* upper_ptr = reinterpret_cast<int32_t*>(2);
  fidl::unowned_ptr<int32_t> lower(lower_ptr);
  fidl::unowned_ptr<int32_t> lower2(lower_ptr);
  fidl::unowned_ptr<int32_t> upper(upper_ptr);

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
  std::set<fidl::unowned_ptr<int32_t>> set;
  set.insert(lower);
}

TEST(UnownedPtr, Casting) {
  class Base {};
  class Derived : public Base {};
  Derived d;
  fidl::unowned_ptr<Derived> d_ptr(&d);
  EXPECT_EQ(static_cast<fidl::unowned_ptr<Base>>(d_ptr).get(), static_cast<Base*>(&d));

  auto vptr = static_cast<fidl::unowned_ptr<void>>(d_ptr);
  EXPECT_EQ(vptr, fidl::unowned_ptr<void>(&d));

  auto d_ptr2 = static_cast<fidl::unowned_ptr<Derived>>(vptr);
  EXPECT_EQ(d_ptr2, d_ptr);
}

TEST(UnownedPtr, UnownedHelper) {
  int32_t val = 1;
  auto ptr = fidl::unowned(&val);
  static_assert(std::is_same<decltype(ptr), fidl::unowned_ptr<int32_t>>::value);
  EXPECT_EQ(ptr.get(), &val);
}
