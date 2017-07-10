// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <mxtl/tests/lfsr.h>
#include <mxtl/ref_counted.h>
#include <mxtl/ref_ptr.h>
#include <mxtl/unique_ptr.h>
#include <mxtl/vector.h>

namespace mxtl {
namespace tests {
namespace {

// Different container classes for the types of objects
// which should be tested within a vector (raw types,
// unique pointers, ref pointers).

using BaseType = size_t;

struct TestObjBase {
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(TestObjBase);
    explicit TestObjBase(BaseType val) : alive_(true), val_(val) { ++live_obj_count_; }
    TestObjBase(TestObjBase&& r) : alive_(r.alive_), val_(r.val_) { r.alive_ = false; }
    TestObjBase& operator=(TestObjBase&& r) {
        val_ = r.val_;
        alive_ = r.alive_;
        r.alive_ = false;
        return *this;
    }
    ~TestObjBase() {
        if (alive_) {
            --live_obj_count_;
        }
    }

    BaseType value() const { return val_; }
    static size_t live_obj_count() { return live_obj_count_; }
    static void ResetLiveObjCount() { live_obj_count_ = 0; }

    bool alive_;
    BaseType val_;

    static size_t live_obj_count_;
};

size_t TestObjBase::live_obj_count_ = 0;

struct BaseTypeTraits {
    using ContainerType = BaseType;
    static ContainerType Create(BaseType val) { return val; }
    static BaseType Base(const ContainerType& c) { return c; }
    // We have no way of managing the "live count" of raw types, so we don't.
    static bool CheckLiveCount(size_t expected) { return true; }
};

struct StructTypeTraits {
    using ContainerType = TestObjBase;
    static ContainerType Create(BaseType val) { return TestObjBase(val); }
    static BaseType Base(const ContainerType& c) { return c.value(); }
    static bool CheckLiveCount(size_t expected) { return TestObjBase::live_obj_count() == expected; }
};

struct UniquePtrTraits {
    using ContainerType = mxtl::unique_ptr<TestObjBase>;

    static ContainerType Create(BaseType val) {
        AllocChecker ac;
        ContainerType ptr(new (&ac) TestObjBase(val));
        MX_ASSERT(ac.check());
        return ptr;
    }
    static BaseType Base(const ContainerType& c) { return c->value(); }
    static bool CheckLiveCount(size_t expected) { return TestObjBase::live_obj_count() == expected; }
};

template <typename T>
struct RefCountedContainer : public mxtl::RefCounted<RefCountedContainer<T>> {
    RefCountedContainer(T v) : val(mxtl::move(v)) {}
    DISALLOW_COPY_ASSIGN_AND_MOVE(RefCountedContainer);
    T val;
};

struct RefPtrTraits {
    using ContainerType = mxtl::RefPtr<RefCountedContainer<TestObjBase>>;

    static ContainerType Create(BaseType val) {
        AllocChecker ac;
        auto ptr = AdoptRef(new (&ac) RefCountedContainer<TestObjBase>(TestObjBase(val)));
        MX_ASSERT(ac.check());
        return ptr;
    }
    static BaseType Base(const ContainerType& c) { return c->val.value(); }
    static bool CheckLiveCount(size_t expected) { return TestObjBase::live_obj_count() == expected; }
};

// Helper classes

template <typename ContainerTraits>
struct Generator {
    using ContainerType = typename ContainerTraits::ContainerType;

    constexpr static BaseType seed = 0xa2328b73e323fd0f;
    BaseType NextBase() { return key_lfsr_.GetNext(); }
    ContainerType NextContainer() { return ContainerTraits::Create(NextBase()); }
    void Reset() { key_lfsr_.SetCore(seed); }
    Lfsr<BaseType> key_lfsr_ = Lfsr<BaseType>(seed);
};

// Actual tests

template <typename ContainerTraits>
bool vector_test_access_release() {
    using ContainerType = typename ContainerTraits::ContainerType;

    BEGIN_TEST;

    Generator<ContainerTraits> gen;
    constexpr size_t size = 10;

    // Create the vector, verify its contents
    {
        mxtl::Vector<ContainerType> vector;
        ASSERT_TRUE(vector.reserve(size), "");
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(i), "");
            ASSERT_TRUE(vector.push_back(gen.NextContainer()), "");
        }
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");

        gen.Reset();
        ContainerType* data = vector.get();
        for (size_t i = 0; i < size; i++) {
            auto base = gen.NextBase();
            // Verify the contents using the [] operator
            ASSERT_EQ(ContainerTraits::Base(vector[i]), base, "");
            // Verify the contents using the underlying array
            ASSERT_EQ(ContainerTraits::Base(data[i]), base, "");
        }

        // Release the vector's underlying array before it is destructed
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");
        vector.reset();
        ASSERT_EQ(vector.size(), 0, "");
        ASSERT_EQ(vector.capacity(), 0, "");
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    END_TEST;
}

struct CountedAllocatorTraits : public DefaultAllocatorTraits {
    static void* Allocate(size_t size) {
        allocation_count++;
        return DefaultAllocatorTraits::Allocate(size);
    }
    static size_t allocation_count;
};

size_t CountedAllocatorTraits::allocation_count = 0;

template <typename ContainerTraits>
bool vector_test_push_back_in_capacity() {
    using ContainerType = typename ContainerTraits::ContainerType;

    BEGIN_TEST;

    Generator<ContainerTraits> gen;
    constexpr size_t size = 10;

    CountedAllocatorTraits::allocation_count = 0;
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");
    {
        mxtl::Vector<ContainerType, CountedAllocatorTraits> vector;
        ASSERT_EQ(CountedAllocatorTraits::allocation_count, 0, "");
        ASSERT_TRUE(vector.reserve(size), "");
        ASSERT_EQ(CountedAllocatorTraits::allocation_count, 1, "");

        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(i), "");
            ASSERT_TRUE(vector.push_back(gen.NextContainer()), "");
        }
        ASSERT_EQ(CountedAllocatorTraits::allocation_count, 1, "");

        gen.Reset();
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ContainerTraits::Base(vector[i]), gen.NextBase(), "");
        }
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    END_TEST;
}

template <typename ContainerTraits>
bool vector_test_push_back_beyond_capacity() {
    using ContainerType = typename ContainerTraits::ContainerType;

    BEGIN_TEST;

    Generator<ContainerTraits> gen;
    constexpr size_t size = 10;

    {
        // Create an empty vector, push back beyond its capacity
        mxtl::Vector<ContainerType> vector;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(i), "");
            ASSERT_TRUE(vector.push_back(gen.NextContainer()), "");
        }

        gen.Reset();
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ContainerTraits::Base(vector[i]), gen.NextBase(), "");
        }
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    END_TEST;
}

template <typename ContainerTraits>
bool vector_test_pop_back() {
    using ContainerType = typename ContainerTraits::ContainerType;

    BEGIN_TEST;

    Generator<ContainerTraits> gen;
    constexpr size_t size = 10;

    {
        // Create a vector filled with objects
        mxtl::Vector<ContainerType> vector;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(i), "");
            ASSERT_TRUE(vector.push_back(gen.NextContainer()), "");
        }

        gen.Reset();
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ContainerTraits::Base(vector[i]), gen.NextBase(), "");
        }

        // Pop one at a time, and check the vector is still valid
        while (vector.size()) {
            vector.pop_back();
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(vector.size()), "");
            gen.Reset();
            for (size_t i = 0; i < vector.size(); i++) {
                ASSERT_EQ(ContainerTraits::Base(vector[i]), gen.NextBase(), "");
            }
        }

        ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    END_TEST;
}

struct FailingAllocatorTraits {
    static void* Allocate(size_t size) { return nullptr; }
    static void Deallocate(void* object) { return; }
};

template <typename ContainerType, size_t S>
struct PartiallyFailingAllocatorTraits : public DefaultAllocatorTraits {
    static void* Allocate(size_t size) {
        if (size <= sizeof(ContainerType) * S) {
            return DefaultAllocatorTraits::Allocate(size);
        }
        return nullptr;
    }
};

template <typename ContainerTraits>
bool vector_test_allocation_failure() {
    using ContainerType = typename ContainerTraits::ContainerType;

    BEGIN_TEST;

    Generator<ContainerTraits> gen;
    constexpr size_t size = 32;

    // Test that a failing allocator cannot take on additional elements
    {
        mxtl::Vector<ContainerType, FailingAllocatorTraits> vector;
        ASSERT_TRUE(vector.reserve(0));
        ASSERT_FALSE(vector.reserve(1));
        ASSERT_FALSE(vector.reserve(size));

        ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");
        ASSERT_FALSE(vector.push_back(gen.NextContainer()), "");
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    // Test that a partially failing allocator stops taking on additional
    // elements
    {
        mxtl::Vector<ContainerType, PartiallyFailingAllocatorTraits<ContainerType, size>> vector;
        ASSERT_TRUE(vector.reserve(0));
        ASSERT_TRUE(vector.reserve(1));
        ASSERT_TRUE(vector.reserve(size));
        ASSERT_EQ(vector.capacity(), size, "");

        ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");
        gen.Reset();
        while (vector.size() < size) {
            ASSERT_TRUE(vector.push_back(gen.NextContainer()), "");
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(vector.size()), "");
        }
        ASSERT_FALSE(vector.push_back(gen.NextContainer()), "");
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");
        ASSERT_EQ(vector.size(), size, "");
        ASSERT_EQ(vector.capacity(), size, "");

        // All the elements we were able to push back should still be present
        gen.Reset();
        for (size_t i = 0; i < vector.size(); i++) {
            ASSERT_EQ(ContainerTraits::Base(vector[i]), gen.NextBase(), "");
        }
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    END_TEST;
}

template <typename ContainerTraits>
bool vector_test_move() {
    using ContainerType = typename ContainerTraits::ContainerType;

    BEGIN_TEST;

    Generator<ContainerTraits> gen;
    constexpr size_t size = 10;

    // Test move constructor
    {
        mxtl::Vector<ContainerType> vectorA;
        ASSERT_TRUE(vectorA.is_empty(), "");
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(i), "");
            ASSERT_TRUE(vectorA.push_back(gen.NextContainer()), "");
        }

        gen.Reset();
        ASSERT_FALSE(vectorA.is_empty(), "");
        ASSERT_EQ(vectorA.size(), size, "");
        mxtl::Vector<ContainerType> vectorB(mxtl::move(vectorA));
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");
        ASSERT_TRUE(vectorA.is_empty(), "");
        ASSERT_EQ(vectorA.size(), 0, "");
        ASSERT_EQ(vectorB.size(), size, "");
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ContainerTraits::Base(vectorB[i]), gen.NextBase(), "");
        }
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    // Test move assignment operator
    {
        gen.Reset();
        mxtl::Vector<ContainerType> vectorA;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(i), "");
            ASSERT_TRUE(vectorA.push_back(gen.NextContainer()), "");
        }

        gen.Reset();
        ASSERT_EQ(vectorA.size(), size, "");
        mxtl::Vector<ContainerType> vectorB;
        vectorB = mxtl::move(vectorA);
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");
        ASSERT_EQ(vectorA.size(), 0, "");
        ASSERT_EQ(vectorB.size(), size, "");
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ContainerTraits::Base(vectorB[i]), gen.NextBase(), "");
        }
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size), "");
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    END_TEST;
}

template <typename ContainerTraits>
bool vector_test_swap() {
    using ContainerType = typename ContainerTraits::ContainerType;

    BEGIN_TEST;

    Generator<ContainerTraits> gen;
    constexpr size_t size = 32;

    {
        mxtl::Vector<ContainerType> vectorA;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(i), "");
            ASSERT_TRUE(vectorA.push_back(gen.NextContainer()), "");
        }
        mxtl::Vector<ContainerType> vectorB;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(size + i), "");
            ASSERT_TRUE(vectorB.push_back(gen.NextContainer()), "");
        }

        gen.Reset();

        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ContainerTraits::Base(vectorA[i]), gen.NextBase(), "");
        }
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ContainerTraits::Base(vectorB[i]), gen.NextBase(), "");
        }

        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size * 2), "");
        vectorA.swap(vectorB);
        ASSERT_TRUE(ContainerTraits::CheckLiveCount(size * 2), "");

        gen.Reset();

        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ContainerTraits::Base(vectorB[i]), gen.NextBase(), "");
        }
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ContainerTraits::Base(vectorA[i]), gen.NextBase(), "");
        }
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    END_TEST;
}

template <typename ContainerTraits>
bool vector_test_iterator() {
    using ContainerType = typename ContainerTraits::ContainerType;

    BEGIN_TEST;

    Generator<ContainerTraits> gen;
    constexpr size_t size = 32;

    {
        mxtl::Vector<ContainerType> vector;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ContainerTraits::CheckLiveCount(i), "");
            ASSERT_TRUE(vector.push_back(gen.NextContainer()), "");
        }

        gen.Reset();
        for (auto& e : vector) {
            auto base = gen.NextBase();
            ASSERT_EQ(ContainerTraits::Base(e), base, "");
            // Take the element out, and put it back... just to check
            // that we can.
            auto other = mxtl::move(e);
            e = mxtl::move(other);
            ASSERT_EQ(ContainerTraits::Base(e), base, "");
        }

        gen.Reset();
        const auto* cvector = &vector;
        for (const auto& e : *cvector) {
            ASSERT_EQ(ContainerTraits::Base(e), gen.NextBase(), "");
        }
    }
    ASSERT_TRUE(ContainerTraits::CheckLiveCount(0), "");

    END_TEST;
}

}  // namespace anonymous

BEGIN_TEST_CASE(vector_tests)
RUN_TEST((vector_test_access_release<BaseTypeTraits>))
RUN_TEST((vector_test_access_release<StructTypeTraits>))
RUN_TEST((vector_test_access_release<UniquePtrTraits>))
RUN_TEST((vector_test_access_release<RefPtrTraits>))
RUN_TEST((vector_test_push_back_in_capacity<BaseTypeTraits>))
RUN_TEST((vector_test_push_back_in_capacity<StructTypeTraits>))
RUN_TEST((vector_test_push_back_in_capacity<UniquePtrTraits>))
RUN_TEST((vector_test_push_back_in_capacity<RefPtrTraits>))
RUN_TEST((vector_test_push_back_beyond_capacity<BaseTypeTraits>))
RUN_TEST((vector_test_push_back_beyond_capacity<StructTypeTraits>))
RUN_TEST((vector_test_push_back_beyond_capacity<UniquePtrTraits>))
RUN_TEST((vector_test_push_back_beyond_capacity<RefPtrTraits>))
RUN_TEST((vector_test_pop_back<BaseTypeTraits>))
RUN_TEST((vector_test_pop_back<StructTypeTraits>))
RUN_TEST((vector_test_pop_back<UniquePtrTraits>))
RUN_TEST((vector_test_pop_back<RefPtrTraits>))
RUN_TEST((vector_test_allocation_failure<BaseTypeTraits>))
RUN_TEST((vector_test_allocation_failure<StructTypeTraits>))
RUN_TEST((vector_test_allocation_failure<UniquePtrTraits>))
RUN_TEST((vector_test_allocation_failure<RefPtrTraits>))
RUN_TEST((vector_test_move<BaseTypeTraits>))
RUN_TEST((vector_test_move<StructTypeTraits>))
RUN_TEST((vector_test_move<UniquePtrTraits>))
RUN_TEST((vector_test_move<RefPtrTraits>))
RUN_TEST((vector_test_swap<BaseTypeTraits>))
RUN_TEST((vector_test_swap<StructTypeTraits>))
RUN_TEST((vector_test_swap<UniquePtrTraits>))
RUN_TEST((vector_test_swap<RefPtrTraits>))
RUN_TEST((vector_test_iterator<BaseTypeTraits>))
RUN_TEST((vector_test_iterator<StructTypeTraits>))
RUN_TEST((vector_test_iterator<UniquePtrTraits>))
RUN_TEST((vector_test_iterator<RefPtrTraits>))

END_TEST_CASE(vector_tests)

}  // namespace tests
}  // namespace mxtl
