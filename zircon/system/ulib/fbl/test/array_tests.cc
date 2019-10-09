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

}  // namespace
