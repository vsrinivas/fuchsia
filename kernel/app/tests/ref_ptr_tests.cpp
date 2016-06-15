// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <app/tests.h>
#include <stdio.h>
#include <unittest.h>
#include <utils/ref_ptr.h>

#include "ref_call_counter.h"

extern "C" int ref_ptr_tests(int argc, const cmd_args* argv)
{
    BEGIN_TEST;
    using RefCallPtr = utils::RefPtr<RefCallCounter>;

    RefCallCounter counter;
    RefCallPtr ptr = utils::AdoptRef<RefCallCounter>(&counter);

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
        RefCallPtr ptr2 = utils::move(ptr);

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

    printf("all tests done\n");
    END_TEST;
}
