// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/new.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/type_support.h>
#include <stdio.h>
#include <unittest/unittest.h>

namespace {

struct CountingDeleter;

class RefCallCounter {
public:
    RefCallCounter();

    void AddRef();
    bool Release();

    void Adopt() {}

    int add_ref_calls() const { return add_ref_calls_; }
    int release_calls() const { return release_calls_; }
    int destroy_calls() const { return destroy_calls_; }

private:
    int add_ref_calls_;
    int release_calls_;

    friend struct CountingDeleter;
    int destroy_calls_;
};

struct CountingDeleter {
    void operator()(RefCallCounter* p) {
        p->destroy_calls_++;
    }
};

RefCallCounter::RefCallCounter()
    : add_ref_calls_(0u), release_calls_(0u), destroy_calls_(0u) {}

void RefCallCounter::AddRef() {
    add_ref_calls_++;
}
bool RefCallCounter::Release() {
    release_calls_++;
    return add_ref_calls_ == release_calls_;
}

static_assert(mxtl::is_standard_layout<mxtl::RefPtr<RefCallCounter>>::value,
              "mxtl::RefPtr<T>'s should have a standard layout.");

static bool ref_ptr_test() {
    BEGIN_TEST;
    using RefCallPtr = mxtl::RefPtr<RefCallCounter, CountingDeleter>;

    RefCallCounter counter;
    RefCallPtr ptr = mxtl::AdoptRef<RefCallCounter, CountingDeleter>(&counter);

    EXPECT_TRUE(&counter == ptr.get(), ".get() should point to object");
    EXPECT_TRUE(static_cast<bool>(ptr), "operator bool");
    EXPECT_TRUE(&counter == &(*ptr), "operator*");

    // Adoption should not manipulate the refcount.
    EXPECT_EQ(0, counter.add_ref_calls(), "");
    EXPECT_EQ(0, counter.release_calls(), "");
    EXPECT_EQ(0, counter.destroy_calls(), "");

    {
        RefCallPtr ptr2 = ptr;

        // Copying to a new RefPtr should call add once.
        EXPECT_EQ(1, counter.add_ref_calls(), "");
        EXPECT_EQ(0, counter.release_calls(), "");
        EXPECT_EQ(0, counter.destroy_calls(), "");
    }
    // Destroying the new RefPtr should release once.
    EXPECT_EQ(1, counter.add_ref_calls(), "");
    EXPECT_EQ(1, counter.release_calls(), "");
    EXPECT_EQ(1, counter.destroy_calls(), "");

    {
        RefCallPtr ptr2;

        EXPECT_TRUE(!static_cast<bool>(ptr2), "");

        ptr.swap(ptr2);

        // Swapping shouldn't cause any add or release calls, but should update
        // values.
        EXPECT_EQ(1, counter.add_ref_calls(), "");
        EXPECT_EQ(1, counter.release_calls(), "");
        EXPECT_EQ(1, counter.destroy_calls(), "");

        EXPECT_TRUE(!static_cast<bool>(ptr), "");
        EXPECT_TRUE(&counter == ptr2.get(), "");

        ptr2.swap(ptr);
    }

    EXPECT_EQ(1, counter.add_ref_calls(), "");
    EXPECT_EQ(1, counter.release_calls(), "");
    EXPECT_EQ(1, counter.destroy_calls(), "");

    {
        RefCallPtr ptr2 = mxtl::move(ptr);

        // Moving shouldn't cause any add or release but should update values.
        EXPECT_EQ(1, counter.add_ref_calls(), "");
        EXPECT_EQ(1, counter.release_calls(), "");
        EXPECT_EQ(1, counter.destroy_calls(), "");

        EXPECT_FALSE(static_cast<bool>(ptr), "");
        EXPECT_TRUE(&counter == ptr2.get(), "");

        ptr2.swap(ptr);
    }

    // Reset should calls release and clear out the pointer.
    ptr.reset(nullptr);
    EXPECT_EQ(1, counter.add_ref_calls(), "");
    EXPECT_EQ(2, counter.release_calls(), "");
    EXPECT_EQ(1, counter.destroy_calls(), "");
    EXPECT_FALSE(static_cast<bool>(ptr), "");
    EXPECT_FALSE(ptr.get(), "");

    END_TEST;
}

static bool ref_ptr_compare_test() {
    BEGIN_TEST;
    using RefCallPtr = mxtl::RefPtr<RefCallCounter, CountingDeleter>;

    RefCallCounter obj1, obj2;

    RefCallPtr ptr1 = mxtl::AdoptRef<RefCallCounter, CountingDeleter>(&obj1);
    RefCallPtr ptr2 = mxtl::AdoptRef<RefCallCounter, CountingDeleter>(&obj2);
    RefCallPtr also_ptr1 = ptr1;
    RefCallPtr null_ref_ptr;

    EXPECT_TRUE(ptr1 == ptr1, "");
    EXPECT_FALSE(ptr1 != ptr1, "");

    EXPECT_FALSE(ptr1 == ptr2, "");
    EXPECT_TRUE(ptr1 != ptr2, "");

    EXPECT_TRUE(ptr1 == also_ptr1, "");
    EXPECT_FALSE(ptr1 != also_ptr1, "");

    EXPECT_TRUE(ptr1 != null_ref_ptr, "");
    EXPECT_TRUE(ptr1 != nullptr, "");
    EXPECT_TRUE(nullptr != ptr1, "");
    EXPECT_FALSE(ptr1 == null_ref_ptr, "");
    EXPECT_FALSE(ptr1 == nullptr, "");
    EXPECT_FALSE(nullptr == ptr1, "");

    EXPECT_TRUE(null_ref_ptr == nullptr, "");
    EXPECT_FALSE(null_ref_ptr != nullptr, "");
    EXPECT_TRUE(nullptr == null_ref_ptr, "");
    EXPECT_FALSE(nullptr != null_ref_ptr, "");

    END_TEST;
}

namespace upcasting {

class Stats {
public:
     Stats() { }
    ~Stats() { destroy_count_++; }

    static void Reset() {
        adopt_calls_   = 0;
        add_ref_calls_ = 0;
        release_calls_ = 0;
        destroy_count_ = 0;
    }

    void Adopt() { adopt_calls_++; }

    void AddRef() {
        ref_count_++;
        add_ref_calls_++;
    }

    bool Release() {
        ref_count_--;
        release_calls_++;
        return (ref_count_ <= 0);
    }

    static uint32_t adopt_calls()   { return adopt_calls_; }
    static uint32_t add_ref_calls() { return add_ref_calls_; }
    static uint32_t release_calls() { return release_calls_; }
    static uint32_t destroy_count() { return destroy_count_; }

private:
    int ref_count_ = 1;
    static uint32_t adopt_calls_;
    static uint32_t add_ref_calls_;
    static uint32_t release_calls_;
    static uint32_t destroy_count_;
};

uint32_t Stats::adopt_calls_   = 0;
uint32_t Stats::add_ref_calls_ = 0;
uint32_t Stats::release_calls_ = 0;
uint32_t Stats::destroy_count_ = 0;

class A : public Stats {
public:
    virtual ~A() { stuff = 0u; }

private:
    volatile uint32_t stuff;
};

class B {
public:
    ~B() { stuff = 1u; }

private:
    volatile uint32_t stuff;
};

class C : public A, public B {
public:
    ~C() { }
};

class D : public A {
public:
    virtual ~D() { stuff = 2u; }

private:
    volatile uint32_t stuff;
};

template <typename T>
struct custom_delete {
    inline void operator()(T* ptr) const {
        enum { type_must_be_complete = sizeof(T) };
        delete ptr;
    }
};

template <typename UptrType>
static bool handoff_lvalue_fn(const UptrType& ptr) {
    BEGIN_TEST;
    EXPECT_NONNULL(ptr, "");
    END_TEST;
}

template <typename UptrType>
static bool handoff_copy_fn(UptrType ptr) {
    BEGIN_TEST;
    EXPECT_NONNULL(ptr, "");
    END_TEST;
}

template <typename UptrType>
static bool handoff_rvalue_fn(UptrType&& ptr) {
    BEGIN_TEST;
    EXPECT_NONNULL(ptr, "");
    END_TEST;
}

template <typename Base,
          typename Derived,
          typename BaseDeleter = mxtl::default_delete<Base>,
          typename DerivedDeleter = mxtl::default_delete<Derived>>
static bool do_test() {
    BEGIN_TEST;
    AllocChecker ac;

    mxtl::RefPtr<Derived, DerivedDeleter> derived_ptr;

    // Construct RefPtr<Base> with a copy and implicit cast
    Stats::Reset();
    derived_ptr = mxtl::AdoptRef<Derived, DerivedDeleter>(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(0, Stats::add_ref_calls(), "");
        EXPECT_EQ(0, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");

        mxtl::RefPtr<Base, BaseDeleter> base_ptr(derived_ptr);

        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_NONNULL(base_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(0, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");
    }

    // Construct RefPtr<Base> with a move and implicit cast
    {
        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(1, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");

        mxtl::RefPtr<Base, BaseDeleter> base_ptr(mxtl::move(derived_ptr));

        EXPECT_NULL(derived_ptr, "");
        EXPECT_NONNULL(base_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(1, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");
    }

    EXPECT_EQ(1, Stats::adopt_calls(), "");
    EXPECT_EQ(1, Stats::add_ref_calls(), "");
    EXPECT_EQ(2, Stats::release_calls(), "");
    EXPECT_EQ(1, Stats::destroy_count(), "");

    // Assign RefPtr<Base> at declaration time with a copy
    Stats::Reset();
    derived_ptr = mxtl::AdoptRef<Derived, DerivedDeleter>(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(0, Stats::add_ref_calls(), "");
        EXPECT_EQ(0, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");

        mxtl::RefPtr<Base, BaseDeleter> base_ptr = derived_ptr;

        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_NONNULL(base_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(0, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");
    }

    // Assign RefPtr<Base> at declaration time with a mxtl::move
    {
        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(1, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");

        mxtl::RefPtr<Base, BaseDeleter> base_ptr = mxtl::move(derived_ptr);

        EXPECT_NULL(derived_ptr, "");
        EXPECT_NONNULL(base_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(1, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");
    }

    EXPECT_EQ(1, Stats::adopt_calls(), "");
    EXPECT_EQ(1, Stats::add_ref_calls(), "");
    EXPECT_EQ(2, Stats::release_calls(), "");
    EXPECT_EQ(1, Stats::destroy_count(), "");

    // Assign RefPtr<Base> after declaration with a copy
    Stats::Reset();
    derived_ptr = mxtl::AdoptRef<Derived, DerivedDeleter>(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(0, Stats::add_ref_calls(), "");
        EXPECT_EQ(0, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");

        mxtl::RefPtr<Base, BaseDeleter> base_ptr;
        base_ptr = derived_ptr;

        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_NONNULL(base_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(0, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");
    }

    // Assign RefPtr<Base> after declaration with a mxtl::move
    {
        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(1, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");

        mxtl::RefPtr<Base, BaseDeleter> base_ptr;
        base_ptr = mxtl::move(derived_ptr);

        EXPECT_NULL(derived_ptr, "");
        EXPECT_NONNULL(base_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(1, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");
    }

    EXPECT_EQ(1, Stats::adopt_calls(), "");
    EXPECT_EQ(1, Stats::add_ref_calls(), "");
    EXPECT_EQ(2, Stats::release_calls(), "");
    EXPECT_EQ(1, Stats::destroy_count(), "");

    // Pass the pointer to a function as an lvalue reference with an implicit cast
    Stats::Reset();
    derived_ptr = mxtl::AdoptRef<Derived, DerivedDeleter>(new (&ac) Derived());
    ASSERT_TRUE(ac.check(), "");
    {
        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(0, Stats::add_ref_calls(), "");
        EXPECT_EQ(0, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");

        // Note: counter to intuition, we actually do expect this to bump the
        // reference count regardless of what the target function does with the
        // reference-to-pointer passed to it.  We are not passing a const
        // reference to a RefPtr<Derived>; instead we are creating a temp
        // RefPtr<Base> (which is where the addref happens) and then passing a
        // refernce to *that* to the function.
        bool test_res = handoff_lvalue_fn<mxtl::RefPtr<Base, BaseDeleter>>(derived_ptr);
        EXPECT_TRUE(test_res, "");

        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(1, Stats::add_ref_calls(), "");
        EXPECT_EQ(1, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");
    }

    // Pass the pointer to a function with a copy and implicit cast
    {
        bool test_res = handoff_copy_fn<mxtl::RefPtr<Base, BaseDeleter>>(derived_ptr);
        EXPECT_TRUE(test_res, "");

        EXPECT_NONNULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(2, Stats::add_ref_calls(), "");
        EXPECT_EQ(2, Stats::release_calls(), "");
        EXPECT_EQ(0, Stats::destroy_count(), "");
    }

    // Pass the pointer to a function as an rvalue reference and implicit cast
    {
        bool test_res = handoff_rvalue_fn<mxtl::RefPtr<Base, BaseDeleter>>(mxtl::move(derived_ptr));
        EXPECT_TRUE(test_res, "");

        EXPECT_NULL(derived_ptr, "");
        EXPECT_EQ(1, Stats::adopt_calls(), "");
        EXPECT_EQ(2, Stats::add_ref_calls(), "");
        EXPECT_EQ(3, Stats::release_calls(), "");
        EXPECT_EQ(1, Stats::destroy_count(), "");
    }

    END_TEST;
}

static bool ref_ptr_upcast_test() {
    BEGIN_TEST;
    bool test_res;

    // This should work.  C derives from A, A has a virtual destructor, and
    // everything is using the default deleter.
    test_res = do_test<A, C>();
    EXPECT_TRUE(test_res, "");

#if TEST_WILL_NOT_COMPILE || 0
    // This should not work.  C derives from B, but B has no virtual destructor.
    test_res = do_test<B, C>();
    EXPECT_FALSE(test_res, "");
#endif

#if TEST_WILL_NOT_COMPILE || 0
    // This should not work.  D has a virtual destructor, but it is not a base
    // class of C.
    test_res = do_test<D, C>();
    EXPECT_FALSE(test_res, "");
#endif

#if TEST_WILL_NOT_COMPILE || 0
    // This should not work.  A and C have the proper relationship, but we are
    // using a custom deleter for unique_ptr<A>, and C will not implicitly cast
    // itself to a unique_ptr<A> which uses a custom deleter.
    test_res = do_test<A, C, custom_delete<A>>();
    EXPECT_FALSE(test_res, "");
#endif

#if TEST_WILL_NOT_COMPILE || 0
    // This should not work.  A and C have the proper relationship, and we are
    // attempting to implicitly cast to unique_ptr<A, default_delete<A>>, but
    // our C pointer is using a custom deleter.
    test_res = do_test<A, C, mxtl::default_delete<A>, custom_delete<C>>();
    EXPECT_FALSE(test_res, "");
#endif

    END_TEST;
}
}  // namespace upcasting
}  // namespace

BEGIN_TEST_CASE(ref_ptr_tests)
RUN_NAMED_TEST("Ref Pointer", ref_ptr_test)
RUN_NAMED_TEST("Ref Pointer Comparison", ref_ptr_compare_test)
RUN_NAMED_TEST("Ref Pointer Upcast", upcasting::ref_ptr_upcast_test)
END_TEST_CASE(ref_ptr_tests);
