// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unittest/unittest.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/type_support.h>

namespace {

class RefCallCounter {
public:
    RefCallCounter();

    void AddRef();
    bool Release();

    void Adopt() {}

    int add_ref_calls() const { return add_ref_calls_; }
    int release_calls() const { return release_calls_; }

private:
    int add_ref_calls_;
    int release_calls_;
};

RefCallCounter::RefCallCounter()
    : add_ref_calls_(0u), release_calls_(0u) {}

void RefCallCounter::AddRef() {
    add_ref_calls_++;
}
bool RefCallCounter::Release() {
    release_calls_++;
    return false;
}

static_assert(mxtl::is_standard_layout<mxtl::RefPtr<RefCallCounter>>::value,
              "mxtl::RefPtr<T>'s should have a standard layout.");

static bool ref_ptr_test() {
    BEGIN_TEST;
    using RefCallPtr = mxtl::RefPtr<RefCallCounter>;

    RefCallCounter counter;
    RefCallPtr ptr = mxtl::AdoptRef<RefCallCounter>(&counter);

    EXPECT_TRUE(&counter == ptr.get(), ".get() should point to object");
    EXPECT_TRUE(static_cast<bool>(ptr), "operator bool");
    EXPECT_TRUE(&counter == &(*ptr), "operator*");

    // Adoption should not manipulate the refcount.
    EXPECT_EQ(0, counter.add_ref_calls(), "");
    EXPECT_EQ(0, counter.release_calls(), "");

    {
        RefCallPtr ptr2 = ptr;

        // Copying to a new RefPtr should call add once.
        EXPECT_EQ(1, counter.add_ref_calls(), "");
        EXPECT_EQ(0, counter.release_calls(), "");
    }
    // Destroying the new RefPtr should release once.
    EXPECT_EQ(1, counter.add_ref_calls(), "");
    EXPECT_EQ(1, counter.release_calls(), "");

    {
        RefCallPtr ptr2;

        EXPECT_TRUE(!static_cast<bool>(ptr2), "");

        ptr.swap(ptr2);

        // Swapping shouldn't cause any add or release calls, but should update
        // values.
        EXPECT_EQ(1, counter.add_ref_calls(), "");
        EXPECT_EQ(1, counter.release_calls(), "");

        EXPECT_TRUE(!static_cast<bool>(ptr), "");
        EXPECT_TRUE(&counter == ptr2.get(), "");

        ptr2.swap(ptr);
    }

    EXPECT_EQ(1, counter.add_ref_calls(), "");
    EXPECT_EQ(1, counter.release_calls(), "");

    {
        RefCallPtr ptr2 = mxtl::move(ptr);

        // Moving shouldn't cause any add or release but should update values.
        EXPECT_EQ(1, counter.add_ref_calls(), "");
        EXPECT_EQ(1, counter.release_calls(), "");

        EXPECT_FALSE(static_cast<bool>(ptr), "");
        EXPECT_TRUE(&counter == ptr2.get(), "");

        ptr2.swap(ptr);
    }

    // Reset should calls release and clear out the pointer.
    ptr.reset(nullptr);
    EXPECT_EQ(2, counter.release_calls(), "");
    EXPECT_FALSE(static_cast<bool>(ptr), "");
    EXPECT_FALSE(ptr.get(), "");

    END_TEST;
}

static bool ref_ptr_compare_test() {
    BEGIN_TEST;
    using RefCallPtr = mxtl::RefPtr<RefCallCounter>;

    RefCallCounter obj1, obj2;
    RefCallPtr ptr1 = mxtl::AdoptRef<RefCallCounter>(&obj1);
    RefCallPtr ptr2 = mxtl::AdoptRef<RefCallCounter>(&obj2);
    RefCallPtr also_ptr1 = ptr1;
    RefCallPtr null_ref_ptr;

    EXPECT_TRUE (ptr1 == ptr1, "");
    EXPECT_FALSE(ptr1 != ptr1, "");

    EXPECT_FALSE(ptr1 == ptr2, "");
    EXPECT_TRUE (ptr1 != ptr2, "");

    EXPECT_TRUE (ptr1 == also_ptr1, "");
    EXPECT_FALSE(ptr1 != also_ptr1, "");

    EXPECT_TRUE (ptr1 != null_ref_ptr, "");
    EXPECT_TRUE (ptr1 != nullptr, "");
    EXPECT_TRUE (nullptr != ptr1, "");
    EXPECT_FALSE(ptr1 == null_ref_ptr, "");
    EXPECT_FALSE(ptr1 == nullptr, "");
    EXPECT_FALSE(nullptr == ptr1, "");

    EXPECT_TRUE (null_ref_ptr == nullptr, "");
    EXPECT_FALSE(null_ref_ptr != nullptr, "");
    EXPECT_TRUE (nullptr == null_ref_ptr, "");
    EXPECT_FALSE(nullptr != null_ref_ptr, "");

    END_TEST;
}

} //namespace

BEGIN_TEST_CASE(ref_ptr_tests)
RUN_NAMED_TEST("Ref Pointer", ref_ptr_test)
RUN_NAMED_TEST("Ref Pointer Comparison", ref_ptr_compare_test)
END_TEST_CASE(ref_ptr_tests);
