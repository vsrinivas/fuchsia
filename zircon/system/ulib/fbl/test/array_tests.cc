// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/array.h>

#include <fbl/alloc_checker.h>
#include <fbl/algorithm.h>
#include <unittest/unittest.h>

namespace {

class DestructorSignaler {
public:
    DestructorSignaler() : array(nullptr), result(nullptr) {}
    ~DestructorSignaler() {
      if (array && result)
        *result = array->get();
    }

    fbl::Array<DestructorSignaler>* array;
    DestructorSignaler** result;
};

bool destructor_test() {
    BEGIN_TEST;

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

    END_TEST;
}

bool move_to_const_ctor_test() {
    BEGIN_TEST;

    constexpr size_t kSize = 10;
    fbl::Array<uint32_t> array(new uint32_t[kSize], kSize);
    for (uint32_t i = 0; i < kSize; ++i) {
        array[i] = i;
    }
    uint32_t* array_ptr = array.get();

    fbl::Array<const uint32_t> const_array(std::move(array));
    EXPECT_NULL(array.get());
    EXPECT_EQ(array.size(), 0);
    EXPECT_EQ(const_array.get(), array_ptr);
    EXPECT_EQ(const_array.size(), kSize);
    for (size_t i = 0; i < kSize; ++i) {
        EXPECT_EQ(const_array[i], i);
    }

    END_TEST;
}

bool move_to_const_assignment_test() {
    BEGIN_TEST;

    constexpr size_t kSize = 10;
    fbl::Array<uint32_t> array(new uint32_t[kSize], kSize);
    for (uint32_t i = 0; i < kSize; ++i) {
        array[i] = i;
    }
    uint32_t* array_ptr = array.get();

    fbl::Array<const uint32_t> const_array;
    const_array = std::move(array);
    EXPECT_NULL(array.get());
    EXPECT_EQ(array.size(), 0);
    EXPECT_EQ(const_array.get(), array_ptr);
    EXPECT_EQ(const_array.size(), kSize);
    for (size_t i = 0; i < kSize; ++i) {
        EXPECT_EQ(const_array[i], i);
    }

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(array_tests)
RUN_NAMED_TEST("destructor test", destructor_test)
RUN_TEST(move_to_const_ctor_test)
RUN_TEST(move_to_const_assignment_test)
END_TEST_CASE(array_tests)
