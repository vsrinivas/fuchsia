// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/alloc_checker.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

#include <atomic>
#include <memory>
#include <utility>

namespace fbl {
namespace tests {
namespace intrusive_containers {

// All test objects derive from a simple base class which keeps track of how
// many of the object are currently alive.
class TestObjBase {
public:
    explicit TestObjBase(size_t) { ++live_obj_count_; }
    ~TestObjBase() { --live_obj_count_; }

    static size_t live_obj_count() { return live_obj_count_; }
    static void ResetLiveObjCount() { live_obj_count_ = 0; }

private:
    static size_t live_obj_count_;
};

// The base class for keyed test objects.  Implements the storage for a key as
// well as the default traits accessor and a set method for use by tests.
template <typename KeyType>
class KeyedTestObjBase : public TestObjBase {
public:
    explicit KeyedTestObjBase(size_t val)
        : TestObjBase(val),
          key_(static_cast<KeyType>(val)) { }

    KeyType GetKey() const { return key_; }
    void SetKey(KeyType key) { key_ = key; }

private:
    KeyType key_;
};

// The base class for hash-able test objects.  Implements a default hash
// function accessor as well as inheriting from KeyedTestObjBase
template <typename KeyType, typename HashType, HashType kNumBuckets>
class HashedTestObjBase : public KeyedTestObjBase<KeyType>  {
public:
    explicit HashedTestObjBase(size_t val) : KeyedTestObjBase<KeyType>(val) { }

    static HashType GetHash(const KeyType& key) {
        // Our simple hash function just multiplies by a big prime and mods by
        // the number of buckets.
        return (static_cast<HashType>(key) * 0xcf2fd713) % kNumBuckets;
    }
};

// A 'test' custom deleter for use when testing managed pointer type which have
// support for template define custom deleters.
template <typename T>
struct TestCustomDeleter {
public:
    constexpr TestCustomDeleter() = default;
    void operator()(T* ptr) const {
        delete_count_.fetch_add(1u);
        delete ptr;
    }

    static void reset_delete_count() { delete_count_.store(0); }
    static size_t delete_count() { return delete_count_.load(); }

private:
    static std::atomic<size_t> delete_count_;
};

// Container test objects are objects which...
//
// 1) Store a size_t value
// 2) Store a 'visited' flag for use when testing iterators
// 3) Derive from TestObjBase (so that live object counts are maintained)
// 4) Exercise the base class helper for the container which makes an object
//    containable (SinglyLinkedListable for SinglyLinkedList, etc...)
// 5) Have storage of the appropriate type to exist in another version of the
//    container being exercised.
template <typename... ContainerTraits>
class TestObj;

class WAVLTreeChecker;

template <typename ContainerTraits_>
class TestObj<ContainerTraits_> :
    public ContainerTraits_::TestObjBaseType,
    public ContainerTraits_::ContainableBaseClass,
    public std::conditional_t<ContainerTraits_::ContainerType::PtrTraits::CanCopy,
                              typename ContainerTraits_::TaggedContainableBaseClasses,
                              fbl::DefaultObjectTag> { // just an empty struct otherwise
public:
    using ContainerTraits    = ContainerTraits_;
    using ContainerStateType = typename ContainerTraits::ContainerStateType;
    using PtrTraits          = typename ContainerStateType::PtrTraits;

    explicit TestObj(size_t val)
        : ContainerTraits_::TestObjBaseType(val),
          val_(val) { }

    size_t value() const { return val_; }
    const void* raw_ptr() const { return this; }

    // Note: the visit method needs to be const (and the visited_count_ member mutable) so we can
    // test const_iterators.
    void Visit() const { ++visited_count_; }
    void ResetVisitedCount() { visited_count_ = 0; }
    size_t visited_count() const { return visited_count_; }

    bool operator==(const TestObj<ContainerTraits>& other) const { return this == &other; }
    bool operator!=(const TestObj<ContainerTraits>& other) const { return this != &other; }

private:
    friend typename ContainerTraits::OtherContainerTraits;

    size_t val_;
    mutable size_t visited_count_ = 0;
    typename ContainerTraits::OtherContainerStateType other_container_state_;
};

// RefedTestObj is a ref-counted version of TestObj for use with RefPtr<> tests.
template <typename ContainerTraits>
class RefedTestObj : public TestObj<ContainerTraits>,
                     public RefCounted<RefedTestObj<ContainerTraits>> {
public:
    explicit RefedTestObj(size_t val) : TestObj<ContainerTraits>(val) { }
};

// Basic pointer type definitions for the 5 types of currently support pointers
//
// Used by the macros which generate the various test environmements
//
// 1) Foo* (unmanaged/raw)
// 2) fbl::unique_ptr<Foo>
// 3) std::unique_ptr<Foo>
// 4) std::unique_ptr<Foo, CustomDeleter>
// 5) fbl::RefPtr<Foo>
//
namespace ptr_type {

template <typename _ObjType>
struct Unmanaged { using type = _ObjType*; };

template <typename _ObjType>
struct UniquePtr { using type = ::fbl::unique_ptr<_ObjType>; };

template <typename _ObjType>
struct StdUniquePtrDefaultDeleter { using type = ::std::unique_ptr<_ObjType>; };

template <typename _ObjType>
struct StdUniquePtrCustomDeleter {
    using type = ::std::unique_ptr<_ObjType, TestCustomDeleter<_ObjType>>;
};

template <typename _ObjType>
struct RefPtr { using type = ::fbl::RefPtr<_ObjType>; };

}  // namespace ptr_type

// Test trait structures contain utilities which define test behavior for the
// five types of pointers which are managed by intrusive containers (see above).
//
// Defined behaviors include...
//
// 1) Allocating a valid version of a pointer to a TestObj of the proper type.
// 2) "Transferring" a pointer (eg. copying if the pointer type supports copying,
//    otherwise moving).
// 3) Testing to see if a pointer to an object was properly transferred into a
//    container.
// 4) Testing to see if a pointer to an object was properly moved into a
//    container.
// 5) Checking to see if the number of times an associated custom deleter was
//    invoked.
// 5) Resetting any assoicated custom deleter state.
template <typename _ObjType>
struct UnmanagedTestTraits {
    using ObjType       = _ObjType;
    using PtrType       = typename ptr_type::Unmanaged<ObjType>::type;
    using ConstPtrType  = const ObjType*;
    using ContainerType = typename ObjType::ContainerTraits::ContainerType;

    static PtrType CreateObject(size_t value) {
        AllocChecker ac;
        auto r = new (&ac) ObjType(value);
        return ac.check() ? r : nullptr;
    }

    static void ReleaseObject(PtrType& ptr) {
        delete ptr;
        ptr = nullptr;
    }

    static bool CheckCustomDeleteInvocations(size_t expected) { return true; }
    static void ResetCustomDeleter() { }

    // Unmanaged pointers never get cleared when being moved or transferred.
    static inline PtrType& Transfer(PtrType& ptr)       { return ptr; }
    static bool WasTransferred(const ConstPtrType& ptr) { return ptr != nullptr; }
    static bool WasMoved (const ConstPtrType& ptr)      { return ptr != nullptr; }
};

template <typename _ObjType>
struct UniquePtrTestTraits {
    using ObjType       = _ObjType;
    using PtrType       = typename ptr_type::UniquePtr<ObjType>::type;
    using ConstPtrType  = const PtrType;
    using ContainerType = typename ObjType::ContainerTraits::ContainerType;

    static PtrType CreateObject(size_t value) {
        AllocChecker ac;
        auto r = new (&ac) ObjType(value);
        return PtrType(ac.check() ? r : nullptr);
    }

    static void ReleaseObject(PtrType& ptr) {
        ptr = nullptr;
    }

    static bool CheckCustomDeleteInvocations(size_t expected) { return true; }
    static void ResetCustomDeleter() { }

    // Unique pointers always get cleared when being moved or transferred.
    static inline PtrType&& Transfer(PtrType& ptr)      { return std::move(ptr); }
    static bool WasTransferred(const ConstPtrType& ptr) { return ptr == nullptr; }
    static bool WasMoved (const ConstPtrType& ptr)      { return ptr == nullptr; }
};

template <typename _ObjType>
struct StdUniquePtrDefaultDeleterTestTraits {
    using ObjType       = _ObjType;
    using PtrType       = typename ptr_type::StdUniquePtrDefaultDeleter<ObjType>::type;
    using ConstPtrType  = const PtrType;
    using ContainerType = typename ObjType::ContainerTraits::ContainerType;

    static PtrType CreateObject(size_t value) {
        AllocChecker ac;
        auto r = new (&ac) ObjType(value);
        return PtrType(ac.check() ? r : nullptr);
    }

    static void ReleaseObject(PtrType& ptr) {
        ptr = nullptr;
    }

    static bool CheckCustomDeleteInvocations(size_t expected) { return true; }
    static void ResetCustomDeleter() { }

    // Unique pointers always get cleared when being moved or transferred.
    static inline PtrType&& Transfer(PtrType& ptr)      { return std::move(ptr); }
    static bool WasTransferred(const ConstPtrType& ptr) { return ptr == nullptr; }
    static bool WasMoved (const ConstPtrType& ptr)      { return ptr == nullptr; }
};

template <typename _ObjType>
struct StdUniquePtrCustomDeleterTestTraits {
    using ObjType       = _ObjType;
    using PtrType       = typename ptr_type::StdUniquePtrCustomDeleter<ObjType>::type;
    using ConstPtrType  = const PtrType;
    using ContainerType = typename ObjType::ContainerTraits::ContainerType;

    static PtrType CreateObject(size_t value) {
        AllocChecker ac;
        auto r = new (&ac) ObjType(value);
        return PtrType(ac.check() ? r : nullptr);
    }

    static void ReleaseObject(PtrType& ptr) {
        ptr = nullptr;
    }

    static bool CheckCustomDeleteInvocations(size_t expected) {
        BEGIN_HELPER;
        EXPECT_EQ(expected, TestCustomDeleter<_ObjType>::delete_count());
        END_HELPER;
    }

    static void ResetCustomDeleter() {
        TestCustomDeleter<_ObjType>::reset_delete_count();
    }

    // Unique pointers always get cleared when being moved or transferred.
    static inline PtrType&& Transfer(PtrType& ptr)      { return std::move(ptr); }
    static bool WasTransferred(const ConstPtrType& ptr) { return ptr == nullptr; }
    static bool WasMoved (const ConstPtrType& ptr)      { return ptr == nullptr; }
};

template <typename _ObjType>
struct RefPtrTestTraits {
    using ObjType       = _ObjType;
    using PtrType       = typename ptr_type::RefPtr<ObjType>::type;
    using ConstPtrType  = const PtrType;
    using ContainerType = typename ObjType::ContainerTraits::ContainerType;

    static PtrType CreateObject(size_t value) {
        AllocChecker ac;
        auto r = new (&ac) ObjType(value);
        return AdoptRef(ac.check() ? r : nullptr);
    }

    static void ReleaseObject(PtrType& ptr) {
        ptr = nullptr;
    }

    static bool CheckCustomDeleteInvocations(size_t expected) { return true; }
    static void ResetCustomDeleter() { }

    // RefCounted pointers do not get cleared when being transferred, but do get
    // cleared when being moved.
    static inline PtrType& Transfer(PtrType& ptr)       { return ptr; }
    static bool WasTransferred(const ConstPtrType& ptr) { return ptr != nullptr; }
    static bool WasMoved (const ConstPtrType& ptr)      { return ptr == nullptr; }
};

}  // namespace intrusive_containers
}  // namespace tests
}  // namespace fbl
