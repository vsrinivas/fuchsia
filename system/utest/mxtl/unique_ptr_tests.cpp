// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <magenta/cpp.h>
#include <unittest/unittest.h>
#include <mxtl/type_support.h>
#include <mxtl/unique_ptr.h>

static int destroy_count = 0;

struct CountingDeleter {
    void operator()(int* p)
    {
        destroy_count++;
        delete p;
    }
};

using CountingPtr    = mxtl::unique_ptr<int,   CountingDeleter>;
using CountingArrPtr = mxtl::unique_ptr<int[], CountingDeleter>;

static_assert(mxtl::is_standard_layout<int>::value,
              "mxtl::unique_ptr<T>'s should have a standard layout");
static_assert(mxtl::is_standard_layout<CountingPtr>::value,
              "mxtl::unique_ptr<T>'s should have a standard layout");
static_assert(mxtl::is_standard_layout<int[]>::value,
              "mxtl::unique_ptr<T[]>'s should have a standard layout");
static_assert(mxtl::is_standard_layout<CountingArrPtr>::value,
              "mxtl::unique_ptr<T[]>'s should have a standard layout");

static bool uptr_test_scoped_destruction() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and let a unique_ptr fall out of scope.
    {
        CountingPtr ptr(new (&ac) int);
        EXPECT_TRUE(ac.check(), "");
    }

    EXPECT_EQ(1, destroy_count, "");
    END_TEST;
}

static bool uptr_test_move() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and move into another unique_ptr.
    {
        CountingPtr ptr(new (&ac) int);
        EXPECT_TRUE(ac.check(), "");

        CountingPtr ptr2 = mxtl::move(ptr);
        EXPECT_NULL(ptr, "expected ptr to be null");
    }

    EXPECT_EQ(1, destroy_count, "");

    END_TEST;
}

static bool uptr_test_null_scoped_destruction() {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct a null unique_ptr and let it fall out of scope - should not call
    // deleter.
    {
        CountingPtr ptr(nullptr);
    }

    EXPECT_EQ(0, destroy_count, "");

    END_TEST;
}

static bool uptr_test_diff_scope_swap() {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct a pair of unique_ptrs in different scopes, swap them, and verify
    // that the values change places and that the values are destroyed at the
    // correct times.

    AllocChecker ac;
    {
        CountingPtr ptr1(new (&ac) int(4));
        EXPECT_TRUE(ac.check(), "");
        {
            CountingPtr ptr2(new (&ac) int(7));
            EXPECT_TRUE(ac.check(), "");

            ptr1.swap(ptr2);
            EXPECT_EQ(7, *ptr1, "");
            EXPECT_EQ(4, *ptr2, "");
        }
        EXPECT_EQ(1, destroy_count, "");
    }
    EXPECT_EQ(2, destroy_count, "");

    END_TEST;
}

static bool uptr_test_bool_op() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;

    CountingPtr foo(new (&ac) int);
    EXPECT_TRUE(ac.check(), "");
    EXPECT_TRUE(static_cast<bool>(foo), "");

    foo.reset();
    EXPECT_EQ(1, destroy_count, "");
    EXPECT_FALSE(static_cast<bool>(foo), "");

    END_TEST;
}

static bool uptr_test_comparison() {
    BEGIN_TEST;

    AllocChecker ac;
    // Test comparison operators.
    mxtl::unique_ptr<int> null_unique;
    mxtl::unique_ptr<int> lesser_unique(new (&ac) int(1));
    EXPECT_TRUE(ac.check(), "");

    mxtl::unique_ptr<int> greater_unique(new (&ac) int(2));
    EXPECT_TRUE(ac.check(), "");

    EXPECT_NEQ(lesser_unique.get(), greater_unique.get(), "");
    if (lesser_unique.get() > greater_unique.get())
        lesser_unique.swap(greater_unique);

    // Comparison against nullptr
    EXPECT_TRUE(   null_unique == nullptr, "");
    EXPECT_TRUE( lesser_unique != nullptr, "");
    EXPECT_TRUE(greater_unique != nullptr, "");

    EXPECT_TRUE(nullptr ==    null_unique, "");
    EXPECT_TRUE(nullptr !=  lesser_unique, "");
    EXPECT_TRUE(nullptr != greater_unique, "");

    // Comparison against other unique_ptr<>s
    EXPECT_TRUE( lesser_unique  ==  lesser_unique, "");
    EXPECT_FALSE( lesser_unique == greater_unique, "");
    EXPECT_FALSE(greater_unique ==  lesser_unique, "");
    EXPECT_TRUE(greater_unique  == greater_unique, "");

    EXPECT_FALSE( lesser_unique !=  lesser_unique, "");
    EXPECT_TRUE ( lesser_unique != greater_unique, "");
    EXPECT_TRUE (greater_unique !=  lesser_unique, "");
    EXPECT_FALSE(greater_unique != greater_unique, "");

    EXPECT_FALSE( lesser_unique <   lesser_unique, "");
    EXPECT_TRUE ( lesser_unique <  greater_unique, "");
    EXPECT_FALSE(greater_unique <   lesser_unique, "");
    EXPECT_FALSE(greater_unique <  greater_unique, "");

    EXPECT_FALSE( lesser_unique >   lesser_unique, "");
    EXPECT_FALSE( lesser_unique >  greater_unique, "");
    EXPECT_TRUE (greater_unique >   lesser_unique, "");
    EXPECT_FALSE(greater_unique >  greater_unique, "");

    EXPECT_TRUE ( lesser_unique <=  lesser_unique, "");
    EXPECT_TRUE ( lesser_unique <= greater_unique, "");
    EXPECT_FALSE(greater_unique <=  lesser_unique, "");
    EXPECT_TRUE (greater_unique <= greater_unique, "");

    EXPECT_TRUE ( lesser_unique >=  lesser_unique, "");
    EXPECT_FALSE( lesser_unique >= greater_unique, "");
    EXPECT_TRUE (greater_unique >=  lesser_unique, "");
    EXPECT_TRUE (greater_unique >= greater_unique, "");

    END_TEST;
}

static bool uptr_test_array_scoped_destruction() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and let a unique_ptr fall out of scope.
    {
        CountingArrPtr ptr(new (&ac) int[1]);
        EXPECT_TRUE(ac.check(), "");
    }
    EXPECT_EQ(1, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_move() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;
    // Construct and move into another unique_ptr.
    {
        CountingArrPtr ptr(new (&ac) int[1]);
        EXPECT_TRUE(ac.check(), "");

        CountingArrPtr ptr2 = mxtl::move(ptr);
        EXPECT_NULL(ptr, "expected ptr to be null");
    }
    EXPECT_EQ(1, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_null_scoped_destruction() {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct a null unique_ptr and let it fall out of scope - should not call
    // deleter.
    {
        CountingArrPtr ptr(nullptr);
    }
    EXPECT_EQ(0, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_diff_scope_swap() {
    BEGIN_TEST;
    destroy_count = 0;

    // Construct a pair of unique_ptrs in different scopes, swap them, and verify
    // that the values change places and that the values are destroyed at the
    // correct times.
    AllocChecker ac;

    {
        CountingArrPtr ptr1(new (&ac) int[1]);
        EXPECT_TRUE(ac.check(), "");

        ptr1[0] = 4;
        {
            CountingArrPtr ptr2(new (&ac) int[1]);
            EXPECT_TRUE(ac.check(), "");

            ptr2[0] = 7;
            ptr1.swap(ptr2);
            EXPECT_EQ(7, ptr1[0], "");
            EXPECT_EQ(4, ptr2[0], "");
        }
        EXPECT_EQ(1, destroy_count, "");
    }
    EXPECT_EQ(2, destroy_count, "");

    END_TEST;
}

static bool uptr_test_array_bool_op() {
    BEGIN_TEST;
    destroy_count = 0;

    AllocChecker ac;

    CountingArrPtr foo(new (&ac) int[1]);
    EXPECT_TRUE(ac.check(), "");
    EXPECT_TRUE(static_cast<bool>(foo), "");

    foo.reset();
    EXPECT_EQ(1, destroy_count, "");
    EXPECT_FALSE(static_cast<bool>(foo), "");

    END_TEST;
}

static bool uptr_test_array_comparison() {
    BEGIN_TEST;

    AllocChecker ac;

    mxtl::unique_ptr<int[]> null_unique;
    mxtl::unique_ptr<int[]> lesser_unique(new (&ac) int[1]);
    EXPECT_TRUE(ac.check(), "");
    mxtl::unique_ptr<int[]> greater_unique(new (&ac) int[2]);
    EXPECT_TRUE(ac.check(), "");

    EXPECT_NEQ(lesser_unique.get(), greater_unique.get(), "");
    if (lesser_unique.get() > greater_unique.get())
        lesser_unique.swap(greater_unique);

    // Comparison against nullptr
    EXPECT_TRUE(   null_unique == nullptr, "");
    EXPECT_TRUE( lesser_unique != nullptr, "");
    EXPECT_TRUE(greater_unique != nullptr, "");

    EXPECT_TRUE(nullptr ==    null_unique, "");
    EXPECT_TRUE(nullptr !=  lesser_unique, "");
    EXPECT_TRUE(nullptr != greater_unique, "");

    // Comparison against other unique_ptr<>s
    EXPECT_TRUE( lesser_unique  ==  lesser_unique, "");
    EXPECT_FALSE( lesser_unique == greater_unique, "");
    EXPECT_FALSE(greater_unique ==  lesser_unique, "");
    EXPECT_TRUE(greater_unique  == greater_unique, "");

    EXPECT_FALSE( lesser_unique !=  lesser_unique, "");
    EXPECT_TRUE ( lesser_unique != greater_unique, "");
    EXPECT_TRUE (greater_unique !=  lesser_unique, "");
    EXPECT_FALSE(greater_unique != greater_unique, "");

    EXPECT_FALSE( lesser_unique <   lesser_unique, "");
    EXPECT_TRUE ( lesser_unique <  greater_unique, "");
    EXPECT_FALSE(greater_unique <   lesser_unique, "");
    EXPECT_FALSE(greater_unique <  greater_unique, "");

    EXPECT_FALSE( lesser_unique >   lesser_unique, "");
    EXPECT_FALSE( lesser_unique >  greater_unique, "");
    EXPECT_TRUE (greater_unique >   lesser_unique, "");
    EXPECT_FALSE(greater_unique >  greater_unique, "");

    EXPECT_TRUE ( lesser_unique <=  lesser_unique, "");
    EXPECT_TRUE ( lesser_unique <= greater_unique, "");
    EXPECT_FALSE(greater_unique <=  lesser_unique, "");
    EXPECT_TRUE (greater_unique <= greater_unique, "");

    EXPECT_TRUE ( lesser_unique >=  lesser_unique, "");
    EXPECT_FALSE( lesser_unique >= greater_unique, "");
    EXPECT_TRUE (greater_unique >=  lesser_unique, "");
    EXPECT_TRUE (greater_unique >= greater_unique, "");

    END_TEST;
}

namespace upcasting {

class A {
public:
    virtual ~A() { stuff_ = 0; }

private:
    volatile uint32_t stuff_;
};

class B {
public:
    ~B() { stuff_ = 1; }

private:
    volatile uint32_t stuff_;
};

class C : public A, public B {
public:
    ~C() { stuff_ = 2; }

private:
    volatile uint32_t stuff_;
};

class D {
public:
    virtual ~D() { stuff_ = 3; }

private:
    volatile uint32_t stuff_;
};

template <typename T>
struct custom_delete {
    inline void operator()(T* ptr) const {
        enum { type_must_be_complete = sizeof(T) };
        delete ptr;
    }
};

template <typename UptrType>
static bool handoff_fn(UptrType&& ptr) {
    BEGIN_TEST;
    EXPECT_NONNULL(ptr, "");
    END_TEST;
}

class OverloadTestHelper {
public:
    enum class Result {
        None,
        ClassA,
        ClassB,
        ClassD,
    };

    void PassByMove(mxtl::unique_ptr<A>&&) { result_ = Result::ClassA; }
    void PassByMove(mxtl::unique_ptr<D>&&) { result_ = Result::ClassD; }

#if TEST_WILL_NOT_COMPILE || 0
    // Enabling this overload should cause the overload test to fail to compile
    // due to ambiguity (it does not know whether to cast mxtl::unique_ptr<C> to
    // mxtl::unique_ptr<A> or mxtl::unique_ptr<B>)
    void PassByMove(mxtl::unique_ptr<B>&&) { result_ = Result::ClassB; }
#endif

    Result result() const { return result_; }

private:
    Result result_ = Result::None;
};


template <typename Base,
          typename Derived,
          typename BaseDeleter = mxtl::default_delete<Base>,
          typename DerivedDeleter = mxtl::default_delete<Derived>>
static bool test_upcast() {
    BEGIN_TEST;

    AllocChecker ac;

    mxtl::unique_ptr<Derived, DerivedDeleter> derived_ptr;

    // Construct unique_ptr<Base> with a move and implicit cast
    derived_ptr.reset(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        EXPECT_NONNULL(derived_ptr, "");

        mxtl::unique_ptr<Base, BaseDeleter> base_ptr(mxtl::move(derived_ptr));

        EXPECT_NULL(derived_ptr, "");
        EXPECT_NONNULL(base_ptr, "");
    }

    // Assign unique_ptr<Base> at declaration time with a mxtl::move
    derived_ptr.reset(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        EXPECT_NONNULL(derived_ptr, "");

        mxtl::unique_ptr<Base, BaseDeleter> base_ptr = mxtl::move(derived_ptr);

        EXPECT_NULL(derived_ptr, "");
        EXPECT_NONNULL(base_ptr, "");
    }

    // Assign unique_ptr<Base> after declaration with a mxtl::move
    derived_ptr.reset(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        mxtl::unique_ptr<Base, BaseDeleter> base_ptr;
        base_ptr = mxtl::move(derived_ptr);
    }

    // Pass the pointer to a function with a move and an implicit cast
    derived_ptr.reset(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        EXPECT_NONNULL(derived_ptr, "");

        bool test_res = handoff_fn<mxtl::unique_ptr<Base, BaseDeleter>>(mxtl::move(derived_ptr));

        EXPECT_NULL(derived_ptr, "");
        EXPECT_TRUE(test_res, "");
    }

#if TEST_WILL_NOT_COMPILE || 0
    // Construct unique_ptr<Base> without a move.
    derived_ptr.reset(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        mxtl::unique_ptr<Base, BaseDeleter> base_ptr(derived_ptr);
    }
#endif

#if TEST_WILL_NOT_COMPILE || 0
    // Assign unique_ptr<Base> at declaration time without a mxtl::move.
    derived_ptr.reset(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        mxtl::unique_ptr<Base, BaseDeleter> base_ptr = derived_ptr;
    }
#endif

#if TEST_WILL_NOT_COMPILE || 0
    // Assign unique_ptr<Base> after declaration without a mxtl::move.
    derived_ptr.reset(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        mxtl::unique_ptr<Base, BaseDeleter> base_ptr;
        base_ptr = derived_ptr;
    }
#endif

#if TEST_WILL_NOT_COMPILE || 0
    // Pass the pointer to a function with an implicit cast but without a move.
    derived_ptr.reset(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        bool test_res = handoff_fn<mxtl::unique_ptr<Base, BaseDeleter>>(derived_ptr);
        EXPECT_FALSE(test_res, "");
    }
#endif

    END_TEST;
}

static bool uptr_upcasting() {
    BEGIN_TEST;

    bool test_res;

    // This should work.  C derives from A, A has a virtual destructor, and
    // everything is using the default deleter.
    test_res = test_upcast<A, C>();
    EXPECT_TRUE(test_res, "");

#if TEST_WILL_NOT_COMPILE || 0
    // This should not work.  C derives from B, but B has no virtual destructor.
    test_res = test_upcast<B, C>();
    EXPECT_FALSE(test_res, "");
#endif

#if TEST_WILL_NOT_COMPILE || 0
    // This should not work.  D has a virtual destructor, but it is not a base
    // class of C.
    test_res = test_upcast<D, C>();
    EXPECT_FALSE(test_res, "");
#endif

#if TEST_WILL_NOT_COMPILE || 0
    // This should not work.  A and C have the proper relationship, but we are
    // using a custom deleter for unique_ptr<A>, and C will not implicitly cast
    // itself to a unique_ptr<A> which uses a custom deleter.
    test_res = test_upcast<A, C, custom_delete<A>>();
    EXPECT_FALSE(test_res, "");
#endif

#if TEST_WILL_NOT_COMPILE || 0
    // This should not work.  A and C have the proper relationship, and we are
    // attempting to implicitly cast to unique_ptr<A, default_delete<A>>, but
    // our C pointer is using a custom deleter.
    test_res = test_upcast<A, C, mxtl::default_delete<A>, custom_delete<C>>();
    EXPECT_FALSE(test_res, "");
#endif

    // Test overload resolution.  Make a C and the try to pass it to
    // OverloadTestHelper's various overloaded methods.  The compiler should
    // know which version to pick, and it should pick the unique_ptr<A> version,
    // not the unique_ptr<D> version.  If the TEST_WILL_NOT_COMPILE check is
    // enabled in OverloadTestHelper, a unique_ptr<B> version will be enabled as
    // well.  This should cause the build to break because of ambiguity.
    AllocChecker ac;
    mxtl::unique_ptr<C> ptr(new (&ac) C());
    ASSERT_TRUE(ac.check(), "");

    {
        // Now test pass by move.
        OverloadTestHelper helper;
        helper.PassByMove(mxtl::move(ptr));

        EXPECT_NULL(ptr, "");
        EXPECT_EQ(OverloadTestHelper::Result::ClassA, helper.result(), "");
    }

    END_TEST;
}

}

BEGIN_TEST_CASE(unique_ptr)
RUN_NAMED_TEST("Scoped Destruction",               uptr_test_scoped_destruction)
RUN_NAMED_TEST("Move",                             uptr_test_move)
RUN_NAMED_TEST("nullptr Scoped Destruction",       uptr_test_null_scoped_destruction)
RUN_NAMED_TEST("Different Scope Swapping",         uptr_test_diff_scope_swap)
RUN_NAMED_TEST("operator bool",                    uptr_test_bool_op)
RUN_NAMED_TEST("comparison operators",             uptr_test_comparison)
RUN_NAMED_TEST("Array Scoped Destruction",         uptr_test_array_scoped_destruction)
RUN_NAMED_TEST("Array Move",                       uptr_test_array_move)
RUN_NAMED_TEST("Array nullptr Scoped Destruction", uptr_test_array_null_scoped_destruction)
RUN_NAMED_TEST("Array Different Scope Swapping",   uptr_test_array_diff_scope_swap)
RUN_NAMED_TEST("Array operator bool",              uptr_test_array_bool_op)
RUN_NAMED_TEST("Array comparison operators",       uptr_test_array_comparison)
RUN_NAMED_TEST("Upcast tests",                     upcasting::uptr_upcasting)
END_TEST_CASE(unique_ptr);
