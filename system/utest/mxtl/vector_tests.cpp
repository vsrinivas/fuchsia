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

using ValueType = size_t;

struct TestObject {
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(TestObject);
    explicit TestObject(ValueType val) : alive_(true), val_(val) { ++live_obj_count_; }
    TestObject(TestObject&& r) : alive_(r.alive_), val_(r.val_) { r.alive_ = false; }
    TestObject& operator=(TestObject&& r) {
        val_ = r.val_;
        alive_ = r.alive_;
        r.alive_ = false;
        return *this;
    }
    ~TestObject() {
        if (alive_) {
            --live_obj_count_;
        }
    }

    ValueType value() const { return val_; }
    static size_t live_obj_count() { return live_obj_count_; }
    static void ResetLiveObjCount() { live_obj_count_ = 0; }

    bool alive_;
    ValueType val_;

    static size_t live_obj_count_;
};

size_t TestObject::live_obj_count_ = 0;

struct ValueTypeTraits {
    using ItemType = ValueType;
    static ItemType Create(ValueType val) { return val; }
    static ValueType GetValue(const ItemType& c) { return c; }
    // We have no way of managing the "live count" of raw types, so we don't.
    static bool CheckLiveCount(size_t expected) { return true; }
};

struct StructTypeTraits {
    using ItemType = TestObject;
    static ItemType Create(ValueType val) { return TestObject(val); }
    static ValueType GetValue(const ItemType& c) { return c.value(); }
    static bool CheckLiveCount(size_t expected) { return TestObject::live_obj_count() == expected; }
};

struct UniquePtrTraits {
    using ItemType = mxtl::unique_ptr<TestObject>;

    static ItemType Create(ValueType val) {
        AllocChecker ac;
        ItemType ptr(new (&ac) TestObject(val));
        MX_ASSERT(ac.check());
        return ptr;
    }
    static ValueType GetValue(const ItemType& c) { return c->value(); }
    static bool CheckLiveCount(size_t expected) { return TestObject::live_obj_count() == expected; }
};

template <typename T>
struct RefCountedItem : public mxtl::RefCounted<RefCountedItem<T>> {
    RefCountedItem(T v) : val(mxtl::move(v)) {}
    DISALLOW_COPY_ASSIGN_AND_MOVE(RefCountedItem);
    T val;
};

struct RefPtrTraits {
    using ItemType = mxtl::RefPtr<RefCountedItem<TestObject>>;

    static ItemType Create(ValueType val) {
        AllocChecker ac;
        auto ptr = AdoptRef(new (&ac) RefCountedItem<TestObject>(TestObject(val)));
        MX_ASSERT(ac.check());
        return ptr;
    }
    static ValueType GetValue(const ItemType& c) { return c->val.value(); }
    static bool CheckLiveCount(size_t expected) { return TestObject::live_obj_count() == expected; }
};

// Helper classes

template <typename ItemTraits>
struct Generator {
    using ItemType = typename ItemTraits::ItemType;

    constexpr static ValueType seed = 0xa2328b73e323fd0f;
    ValueType NextValue() { return key_lfsr_.GetNext(); }
    ItemType NextItem() { return ItemTraits::Create(NextValue()); }
    void Reset() { key_lfsr_.SetCore(seed); }
    Lfsr<ValueType> key_lfsr_ = Lfsr<ValueType>(seed);
};

// Actual tests

template <typename ItemTraits, size_t size>
bool vector_test_access_release() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;
    // Create the vector, verify its contents
    {
        mxtl::Vector<ItemType> vector;
        ASSERT_TRUE(vector.reserve(size));
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            ASSERT_TRUE(vector.push_back(gen.NextItem()));
        }
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));

        gen.Reset();
        ItemType* data = vector.get();
        for (size_t i = 0; i < size; i++) {
            auto base = gen.NextValue();
            // Verify the contents using the [] operator
            ASSERT_EQ(ItemTraits::GetValue(vector[i]), base);
            // Verify the contents using the underlying array
            ASSERT_EQ(ItemTraits::GetValue(data[i]), base);
        }

        // Release the vector's underlying array before it is destructed
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
        vector.reset();
        ASSERT_EQ(vector.size(), 0);
        ASSERT_EQ(vector.capacity(), 0);
        ASSERT_TRUE(ItemTraits::CheckLiveCount(0));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

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

template <typename ItemTraits, size_t size>
bool vector_test_push_back_in_capacity() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    CountedAllocatorTraits::allocation_count = 0;
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));
    {
        mxtl::Vector<ItemType, CountedAllocatorTraits> vector;
        ASSERT_EQ(CountedAllocatorTraits::allocation_count, 0);
        ASSERT_TRUE(vector.reserve(size));
        ASSERT_EQ(CountedAllocatorTraits::allocation_count, 1);

        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            ASSERT_TRUE(vector.push_back(gen.NextItem()));
        }
        ASSERT_EQ(CountedAllocatorTraits::allocation_count, 1);

        gen.Reset();
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vector[i]), gen.NextValue());
        }
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

template <typename ItemTraits, size_t size>
bool vector_test_push_back_by_const_ref_in_capacity() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    CountedAllocatorTraits::allocation_count = 0;
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));
    {
        mxtl::Vector<ItemType, CountedAllocatorTraits> vector;
        ASSERT_EQ(CountedAllocatorTraits::allocation_count, 0);
        ASSERT_TRUE(vector.reserve(size));
        ASSERT_EQ(CountedAllocatorTraits::allocation_count, 1);

        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            const ItemType item = gen.NextItem();
            ASSERT_TRUE(vector.push_back(item));
        }
        ASSERT_EQ(CountedAllocatorTraits::allocation_count, 1);

        gen.Reset();
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vector[i]), gen.NextValue());
        }
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

template <typename ItemTraits, size_t size>
bool vector_test_push_back_beyond_capacity() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    {
        // Create an empty vector, push back beyond its capacity
        mxtl::Vector<ItemType> vector;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            ASSERT_TRUE(vector.push_back(gen.NextItem()));
        }

        gen.Reset();
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vector[i]), gen.NextValue());
        }
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

template <typename ItemTraits, size_t size>
bool vector_test_push_back_by_const_ref_beyond_capacity() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    {
        // Create an empty vector, push back beyond its capacity
        mxtl::Vector<ItemType> vector;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            const ItemType item = gen.NextItem();
            ASSERT_TRUE(vector.push_back(item));
        }

        gen.Reset();
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vector[i]), gen.NextValue());
        }
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

template <typename ItemTraits, size_t size>
bool vector_test_pop_back() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    {
        // Create a vector filled with objects
        mxtl::Vector<ItemType> vector;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            ASSERT_TRUE(vector.push_back(gen.NextItem()));
        }

        gen.Reset();
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vector[i]), gen.NextValue());
        }

        // Pop one at a time, and check the vector is still valid
        while (vector.size()) {
            vector.pop_back();
            ASSERT_TRUE(ItemTraits::CheckLiveCount(vector.size()));
            gen.Reset();
            for (size_t i = 0; i < vector.size(); i++) {
                ASSERT_EQ(ItemTraits::GetValue(vector[i]), gen.NextValue());
            }
        }

        ASSERT_TRUE(ItemTraits::CheckLiveCount(0));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

struct FailingAllocatorTraits {
    static void* Allocate(size_t size) { return nullptr; }
    static void Deallocate(void* object) { return; }
};

template <typename ItemType, size_t S>
struct PartiallyFailingAllocatorTraits : public DefaultAllocatorTraits {
    static void* Allocate(size_t size) {
        if (size <= sizeof(ItemType) * S) {
            return DefaultAllocatorTraits::Allocate(size);
        }
        return nullptr;
    }
};

template <typename ItemTraits, size_t size>
bool vector_test_allocation_failure() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    // Test that a failing allocator cannot take on additional elements
    {
        mxtl::Vector<ItemType, FailingAllocatorTraits> vector;
        ASSERT_TRUE(vector.reserve(0));
        ASSERT_FALSE(vector.reserve(1));
        ASSERT_FALSE(vector.reserve(size));

        ASSERT_TRUE(ItemTraits::CheckLiveCount(0));
        ASSERT_FALSE(vector.push_back(gen.NextItem()));
        ASSERT_TRUE(ItemTraits::CheckLiveCount(0));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    // Test that a partially failing allocator stops taking on additional
    // elements
    {
        mxtl::Vector<ItemType, PartiallyFailingAllocatorTraits<ItemType, size>> vector;
        ASSERT_TRUE(vector.reserve(0));
        ASSERT_TRUE(vector.reserve(1));
        ASSERT_TRUE(vector.reserve(size));
        ASSERT_EQ(vector.capacity(), size);

        ASSERT_TRUE(ItemTraits::CheckLiveCount(0));
        gen.Reset();
        while (vector.size() < size) {
            ASSERT_TRUE(vector.push_back(gen.NextItem()));
            ASSERT_TRUE(ItemTraits::CheckLiveCount(vector.size()));
        }
        ASSERT_FALSE(vector.push_back(gen.NextItem()));
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
        ASSERT_EQ(vector.size(), size);
        ASSERT_EQ(vector.capacity(), size);

        // All the elements we were able to push back should still be present
        gen.Reset();
        for (size_t i = 0; i < vector.size(); i++) {
            ASSERT_EQ(ItemTraits::GetValue(vector[i]), gen.NextValue());
        }
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

template <typename ItemTraits, size_t size>
bool vector_test_move() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    // Test move constructor
    {
        mxtl::Vector<ItemType> vectorA;
        ASSERT_TRUE(vectorA.is_empty());
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            ASSERT_TRUE(vectorA.push_back(gen.NextItem()));
        }

        gen.Reset();
        ASSERT_FALSE(vectorA.is_empty());
        ASSERT_EQ(vectorA.size(), size);
        mxtl::Vector<ItemType> vectorB(mxtl::move(vectorA));
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
        ASSERT_TRUE(vectorA.is_empty());
        ASSERT_EQ(vectorA.size(), 0);
        ASSERT_EQ(vectorB.size(), size);
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vectorB[i]), gen.NextValue());
        }
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    // Test move assignment operator
    {
        gen.Reset();
        mxtl::Vector<ItemType> vectorA;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            ASSERT_TRUE(vectorA.push_back(gen.NextItem()));
        }

        gen.Reset();
        ASSERT_EQ(vectorA.size(), size);
        mxtl::Vector<ItemType> vectorB;
        vectorB = mxtl::move(vectorA);
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
        ASSERT_EQ(vectorA.size(), 0);
        ASSERT_EQ(vectorB.size(), size);
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vectorB[i]), gen.NextValue());
        }
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

template <typename ItemTraits, size_t size>
bool vector_test_swap() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    {
        mxtl::Vector<ItemType> vectorA;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            ASSERT_TRUE(vectorA.push_back(gen.NextItem()));
        }
        mxtl::Vector<ItemType> vectorB;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(size + i));
            ASSERT_TRUE(vectorB.push_back(gen.NextItem()));
        }

        gen.Reset();

        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vectorA[i]), gen.NextValue());
        }
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vectorB[i]), gen.NextValue());
        }

        ASSERT_TRUE(ItemTraits::CheckLiveCount(size * 2));
        vectorA.swap(vectorB);
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size * 2));

        gen.Reset();

        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vectorB[i]), gen.NextValue());
        }
        for (size_t i = 0; i < size; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vectorA[i]), gen.NextValue());
        }
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

template <typename ItemTraits, size_t size>
bool vector_test_iterator() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    {
        mxtl::Vector<ItemType> vector;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            ASSERT_TRUE(vector.push_back(gen.NextItem()));
        }

        gen.Reset();
        for (auto& e : vector) {
            auto base = gen.NextValue();
            ASSERT_EQ(ItemTraits::GetValue(e), base);
            // Take the element out, and put it back... just to check
            // that we can.
            auto other = mxtl::move(e);
            e = mxtl::move(other);
            ASSERT_EQ(ItemTraits::GetValue(e), base);
        }

        gen.Reset();
        const auto* cvector = &vector;
        for (const auto& e : *cvector) {
            ASSERT_EQ(ItemTraits::GetValue(e), gen.NextValue());
        }
    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

template <typename ItemTraits, size_t size>
bool vector_test_insert_delete() {
    using ItemType = typename ItemTraits::ItemType;

    BEGIN_TEST;

    Generator<ItemTraits> gen;

    {
        mxtl::Vector<ItemType> vector;
        for (size_t i = 0; i < size; i++) {
            ASSERT_TRUE(ItemTraits::CheckLiveCount(i));
            ASSERT_TRUE(vector.insert(i, gen.NextItem()));
        }

        // Insert at position zero and one
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
        ASSERT_TRUE(vector.insert(0, gen.NextItem()));
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size + 1));
        ASSERT_TRUE(vector.insert(1, gen.NextItem()));
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size + 2));
        gen.Reset();

        // Verify the contents
        for (size_t i = 2; i < size + 2; i++) {
            ASSERT_EQ(ItemTraits::GetValue(vector[i]), gen.NextValue());
        }
        ASSERT_EQ(ItemTraits::GetValue(vector[0]), gen.NextValue());
        ASSERT_EQ(ItemTraits::GetValue(vector[1]), gen.NextValue());
        gen.Reset();

        {
            // Erase from position one
            ASSERT_TRUE(ItemTraits::CheckLiveCount(size + 2));
            auto erasedval1 = vector.erase(1);
            // Erase from position zero
            ASSERT_TRUE(ItemTraits::CheckLiveCount(size + 2));
            auto erasedval0 = vector.erase(0);
            ASSERT_TRUE(ItemTraits::CheckLiveCount(size + 2));

            // Verify the remaining contents
            for (size_t i = 0; i < size; i++) {
                ASSERT_EQ(ItemTraits::GetValue(vector[i]), gen.NextValue());
            }
            ASSERT_EQ(ItemTraits::GetValue(erasedval0), gen.NextValue());
            ASSERT_EQ(ItemTraits::GetValue(erasedval1), gen.NextValue());
            ASSERT_TRUE(ItemTraits::CheckLiveCount(size + 2));
        }
        ASSERT_TRUE(ItemTraits::CheckLiveCount(size));
        gen.Reset();

        // Erase the remainder of the vector
        for (size_t i = 0; i < size; i++) {
            vector.erase(0);
        }
        ASSERT_EQ(vector.size(), 0);

    }
    ASSERT_TRUE(ItemTraits::CheckLiveCount(0));

    END_TEST;
}

}  // namespace anonymous

#define RUN_FOR_ALL_TRAITS(test_base, test_size)              \
        RUN_TEST((test_base<ValueTypeTraits, test_size>))     \
        RUN_TEST((test_base<StructTypeTraits, test_size>))    \
        RUN_TEST((test_base<UniquePtrTraits, test_size>))     \
        RUN_TEST((test_base<RefPtrTraits, test_size>))

#define RUN_FOR_ALL(test_base)            \
        RUN_FOR_ALL_TRAITS(test_base, 1)  \
        RUN_FOR_ALL_TRAITS(test_base, 2)  \
        RUN_FOR_ALL_TRAITS(test_base, 10) \
        RUN_FOR_ALL_TRAITS(test_base, 32) \
        RUN_FOR_ALL_TRAITS(test_base, 64) \
        RUN_FOR_ALL_TRAITS(test_base, 100)

BEGIN_TEST_CASE(vector_tests)
RUN_FOR_ALL(vector_test_access_release)
RUN_FOR_ALL(vector_test_push_back_in_capacity)
RUN_FOR_ALL(vector_test_push_back_beyond_capacity)
RUN_FOR_ALL(vector_test_pop_back)
RUN_FOR_ALL(vector_test_allocation_failure)
RUN_FOR_ALL(vector_test_move)
RUN_FOR_ALL(vector_test_swap)
RUN_FOR_ALL(vector_test_iterator)
RUN_FOR_ALL(vector_test_insert_delete)
RUN_TEST((vector_test_push_back_by_const_ref_in_capacity<ValueTypeTraits, 100>))
RUN_TEST((vector_test_push_back_by_const_ref_beyond_capacity<ValueTypeTraits, 100>))
END_TEST_CASE(vector_tests)

}  // namespace tests
}  // namespace mxtl
