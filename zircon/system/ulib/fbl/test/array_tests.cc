// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <zxtest/zxtest.h>

namespace {

class DestructorSignaler {
 public:
  DestructorSignaler() : array(nullptr), result(nullptr) {}
  ~DestructorSignaler() {
    if (array && result)
      *result = array->data();
  }

  fbl::Array<DestructorSignaler>* array;
  DestructorSignaler** result;
};

TEST(ArrayTest, Destructor) {
  DestructorSignaler bogus;
  DestructorSignaler* result = &bogus;

  fbl::AllocChecker ac;
  DestructorSignaler* signalers = new (&ac) DestructorSignaler[2];
  EXPECT_TRUE(ac.check());

  {
    fbl::Array<DestructorSignaler> array(signalers, 2);
    array[0].array = &array;
    array[0].result = &result;
  }

  EXPECT_FALSE(result == &bogus);
  EXPECT_TRUE(result == nullptr);
}

TEST(ArrayTest, MoveToConstCtor) {
  constexpr size_t kSize = 10;
  fbl::Array<uint32_t> array(new uint32_t[kSize], kSize);
  for (uint32_t i = 0; i < kSize; ++i) {
    array[i] = i;
  }
  uint32_t* array_ptr = array.data();

  fbl::Array<const uint32_t> const_array(std::move(array));
  EXPECT_NULL(array.data());
  EXPECT_EQ(array.size(), 0);
  EXPECT_EQ(const_array.data(), array_ptr);
  EXPECT_EQ(const_array.size(), kSize);
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(const_array[i], i);
  }
}

TEST(ArrayTest, MoveToConstAssignment) {
  constexpr size_t kSize = 10;
  fbl::Array<uint32_t> array(new uint32_t[kSize], kSize);
  for (uint32_t i = 0; i < kSize; ++i) {
    array[i] = i;
  }
  uint32_t* array_ptr = array.data();

  fbl::Array<const uint32_t> const_array;
  const_array = std::move(array);
  EXPECT_NULL(array.data());
  EXPECT_EQ(array.size(), 0);
  EXPECT_EQ(const_array.data(), array_ptr);
  EXPECT_EQ(const_array.size(), kSize);
  for (size_t i = 0; i < kSize; ++i) {
    EXPECT_EQ(const_array[i], i);
  }
}

TEST(ArrayTest, MakeArraySimple) {
  constexpr size_t kSize = 10;
  fbl::Array<uint32_t> array = fbl::MakeArray<uint32_t>(kSize);

  // Ensure the correct size array was made.
  EXPECT_EQ(array.size(), kSize);

  // Ensure the underlying array was created and can be written to.
  EXPECT_TRUE(array.data() != nullptr);
  for (uint32_t i = 0; i < kSize; ++i) {
    array[i] = i;
  }
}

TEST(ArrayTest, MakeArrayAllocChecker) {
  constexpr size_t kSize = 10;

  fbl::AllocChecker ac;
  fbl::Array<uint32_t> array = fbl::MakeArray<uint32_t>(&ac, kSize);

  EXPECT_TRUE(ac.check());
  EXPECT_EQ(array.size(), kSize);
  EXPECT_TRUE(array.data() != nullptr);
}

// An object with an overloaded new operator that will always fail.
struct CannotAllocate {
  static void* operator new[](std::size_t sz, fbl::AllocChecker* ac) noexcept {
    ac->arm(sz, false);
    return nullptr;
  }
};

TEST(ArrayTest, MakeArrayFailingAllocChecker) {
  // Attempt to create an array of CannotAllocate objects.
  fbl::AllocChecker ac;
  fbl::Array<CannotAllocate> array = fbl::MakeArray<CannotAllocate>(&ac, 10);

  // Expect it to have failed.
  EXPECT_FALSE(ac.check());
  EXPECT_EQ(array.size(), 0);
  EXPECT_EQ(array.data(), nullptr);
}

TEST(ArrayTest, MakeArrayEmpty) {
  fbl::Array<uint32_t> array = fbl::MakeArray<uint32_t>(0);
  EXPECT_EQ(array.size(), 0);
}

TEST(ArrayTest, MakeArrayDefaultConstructed) {
  constexpr size_t kSize = 10;
  struct MyInt {
    int value = 42;
  };

  fbl::Array<MyInt> array = fbl::MakeArray<MyInt>(kSize);
  EXPECT_EQ(array.size(), kSize);
  for (size_t i = 0; i < kSize; i++) {
    EXPECT_EQ(array[i].value, 42);
  }
}

}  // namespace
