// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <mxalloc/new.h>
#include <mxtl/macros.h>

namespace mxtl {

struct DefaultAllocatorTraits {
    // Allocate receives a request for "size" contiguous bytes.
    // size will always be greater than zero.
    // The return value must be "nullptr" on error, or a non-null
    // pointer on success. This same pointer may later be passed
    // to deallocate when resizing.
    static void* Allocate(size_t size) {
        MX_DEBUG_ASSERT(size > 0);
        AllocChecker ac;
        void* object = new (&ac) char[size]();
        return ac.check() ? object : nullptr;
    }

    // Deallocate receives a pointer to an object which is
    // 1) Either a pointer provided by Allocate, or
    // 2) nullptr.
    // If the pointer is not null, deallocate must free
    // the underlying memory used by the object.
    static void Deallocate(void* object) {
        if (object != nullptr) {
            delete reinterpret_cast<char*>(object);
        }
    }
};

// Vector<> is an implementation of a dynamic array, implementing
// a limited set of functionality of std::vector.
//
// Notably, Vector<> returns information about allocation failures,
// rather than throwing exceptions. Furthermore, Vector<> does
// not allow copying or insertions / deletions from anywhere except
// the end.
//
// This Vector supports O(1) indexing and O(1) (amortized) insertion and
// deletion at the end (due to possible reallocations during push_back
// and pop_back).
template <typename T, typename AllocatorTraits = DefaultAllocatorTraits>
class Vector {
public:
    // move semantics only
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Vector);

    constexpr Vector() : ptr_(nullptr), size_(0U), capacity_(0U) {}

    Vector(Vector&& other) : ptr_(nullptr), size_(other.size_), capacity_(other.capacity_) {
        ptr_ = other.release();
    }

    Vector& operator=(Vector&& o) {
        auto size = o.size_;
        auto capacity = o.capacity_;
        reset(o.release(), size, capacity);
        return *this;
    }

    size_t size() const {
        return size_;
    }

    size_t capacity() const {
        return capacity_;
    }

    ~Vector() {
        reset();
    }

    // Reserve enough size to hold at least capacity elements.
    // Returns true on success, false on allocation failure.
    bool reserve(size_t capacity) __WARN_UNUSED_RESULT {
        if (capacity <= capacity_) {
            return true;
        }
        return reallocate(capacity);
    }

    void reset() {
        reset(nullptr, 0U, 0U);
    }

    void swap(Vector& other) {
        T* t = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = t;
        size_t size = size_;
        size_t capacity = capacity_;

        size_ = other.size_;
        capacity_ = other.capacity_;

        other.size_ = size;
        other.capacity_ = capacity;
    }

    bool push_back(T&& value) __WARN_UNUSED_RESULT {
        MX_DEBUG_ASSERT(size_ <= capacity_);
        if (size_ == capacity_) {
            size_t newCapacity = capacity_ < kCapacityMinimum ? kCapacityMinimum :
                    capacity_ * kCapacityGrowthFactor;
            if (!reallocate(newCapacity)) {
                return false;
            }
        }
        ptr_[size_++] = mxtl::forward<T>(value);
        return true;
    }

    void pop_back() {
        MX_DEBUG_ASSERT(size_ > 0);
        ptr_[--size_].~T();
        if (size_ * kCapacityGrowthFactor < capacity_ &&
            capacity_ > kCapacityMinimum) {
            // Try to shrink the underlying storage
            static_assert((kCapacityMinimum + 1) >= kCapacityShrinkFactor,
                          "Capacity heuristics risk reallocating to zero capacity");
            size_t newCapacity = capacity_ / kCapacityShrinkFactor;
            reallocate(newCapacity);
        }
    }

    T* get() const {
        return ptr_;
    }

    bool is_empty() const {
        return size_ == 0;
    }

    T& operator[](size_t i) const {
        MX_DEBUG_ASSERT(i < size_);
        return ptr_[i];
    }

    T* begin() const {
        return ptr_;
    }

    T* end() const {
        return &ptr_[size_];
    }

private:
    // Forces capacity to become newCapcity.
    // Returns true on success, false on failure.
    // If reallocate fails, the old "ptr_" array is unmodified.
    bool reallocate(size_t newCapacity) {
        MX_DEBUG_ASSERT(newCapacity > 0);
        MX_DEBUG_ASSERT(newCapacity >= size_);
        auto newPtr = reinterpret_cast<T*>(AllocatorTraits::Allocate(newCapacity * sizeof(T)));
        if (newPtr == nullptr) {
            return false;
        }
        for (size_t i = 0; i < size_; i++) {
            newPtr[i] = mxtl::move(ptr_[i]);
        }
        AllocatorTraits::Deallocate(ptr_);
        capacity_ = newCapacity;
        ptr_ = newPtr;
        return true;
    }

    // Release returns the underlying storage of the vector,
    // while emptying out the vector itself (so it can be destroyed
    // without deleting the release storage).
    T* release() {
        T* t = ptr_;
        ptr_ = nullptr;
        size_ = 0;
        capacity_ = 0;
        return t;
    }

    void reset(T* t, size_t size, size_t capacity) {
        MX_DEBUG_ASSERT(size <= capacity);
        while (size_ > 0) {
            ptr_[--size_].~T();
        }
        T* ptr = ptr_;
        ptr_ = t;
        size_ = size;
        capacity_ = capacity;
        AllocatorTraits::Deallocate(ptr);
    }

    T* ptr_;
    size_t size_;
    size_t capacity_;

    static constexpr size_t kCapacityMinimum = 16;
    static constexpr size_t kCapacityGrowthFactor = 2;
    static constexpr size_t kCapacityShrinkFactor = 4;
};

}  // namespace mxtl
