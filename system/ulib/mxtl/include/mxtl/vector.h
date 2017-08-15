// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string.h>

#include <magenta/assert.h>
#include <mxtl/alloc_checker.h>
#include <mxtl/macros.h>
#include <mxtl/new.h>
#include <mxtl/type_support.h>

namespace mxtl {

namespace internal {

template <typename U>
using remove_cv_ref = typename remove_cv<typename remove_reference<U>::type>::type;

}  // namespace internal

struct DefaultAllocatorTraits {
    // Allocate receives a request for "size" contiguous bytes.
    // size will always be greater than zero.
    // The return value must be "nullptr" on error, or a non-null
    // pointer on success. This same pointer may later be passed
    // to deallocate when resizing.
    static void* Allocate(size_t size) {
        MX_DEBUG_ASSERT(size > 0);
        AllocChecker ac;
        void* object = new (&ac) char[size];
        return ac.check() ? object : nullptr;
    }

    // Deallocate receives a pointer to an object which is
    // 1) Either a pointer provided by Allocate, or
    // 2) nullptr.
    // If the pointer is not null, deallocate must free
    // the underlying memory used by the object.
    static void Deallocate(void* object) {
        if (object != nullptr) {
            delete[] reinterpret_cast<char*>(object);
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

    // TODO(smklein): In the future, if we want to be able to push back
    // function pointers and arrays with impunity without requiring exact
    // types, this 'remove_cv_ref' call should probably be replaced with a
    // 'mxtl::decay' call.
    template <typename U,
              typename = typename enable_if<is_same<internal::remove_cv_ref<U>, T>::value>::type>
    bool __WARN_UNUSED_RESULT push_back(U&& value) {
        if (!grow_for_new_element()) {
            return false;
        }
        new (&ptr_[size_++]) T(mxtl::forward<U>(value));
        return true;
    }

    // Insert an element into the |index| position in the vector, shifting
    // all subsequent elements back one position.
    //
    // Returns a bool indicating success (true) or failure (due to lack
    // of memory), like "push_back".
    //
    // Index must be less than or equal to the size of the vector.
    template <typename U,
              typename = typename enable_if<is_same<internal::remove_cv_ref<U>, T>::value>::type>
    bool __WARN_UNUSED_RESULT insert(size_t index, U&& value) {
        MX_DEBUG_ASSERT(index <= size_);
        if (!grow_for_new_element()) {
            return false;
        }
        if (index == size_) {
            // Inserting into the end of the vector; nothing to shift
            size_++;
            new (&ptr_[index]) T(mxtl::forward<U>(value));
        } else {
            // Avoid calling both a destructor and move constructor, preferring
            // to simply call a move assignment operator if the index contains
            // a valid (yet already moved-from) object.
            shift_back(index);
            ptr_[index] = mxtl::forward<U>(value);
        }
        return true;
    }

    // Remove an element from the |index| position in the vector, shifting
    // all subsequent elements one position to fill in the gap.
    // Returns the removed element.
    //
    // Index must be less than the size of the vector.
    T erase(size_t index) {
        MX_DEBUG_ASSERT(index < size_);
        auto val = mxtl::move(ptr_[index]);
        shift_forward(index);
        consider_shrinking();
        return mxtl::move(val);
    }

    void pop_back() {
        MX_DEBUG_ASSERT(size_ > 0);
        ptr_[--size_].~T();
        consider_shrinking();
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
    // Moves all objects in the internal storage (at & after index) back by one,
    // leaving an 'empty' object at index.
    // Increases the size of the vector by one.
    template <typename U = T>
    typename enable_if<is_pod<U>::value, void>::type
    shift_back(size_t index) {
        MX_DEBUG_ASSERT(size_ < capacity_);
        MX_DEBUG_ASSERT(size_ > 0);
        size_++;
        memmove(&ptr_[index + 1], &ptr_[index], sizeof(T) * (size_ - (index + 1)));
    }

    template <typename U = T>
    typename enable_if<!is_pod<U>::value, void>::type
    shift_back(size_t index) {
        MX_DEBUG_ASSERT(size_ < capacity_);
        MX_DEBUG_ASSERT(size_ > 0);
        size_++;
        new (&ptr_[size_ - 1]) T(mxtl::move(ptr_[size_ - 2]));
        for (size_t i = size_ - 2; i > index; i--) {
            ptr_[i] = mxtl::move(ptr_[i - 1]);
        }
    }

    // Moves all objects in the internal storage (after index) forward by one.
    // Decreases the size of the vector by one.
    template <typename U = T>
    typename enable_if<is_pod<U>::value, void>::type
    shift_forward(size_t index) {
        MX_DEBUG_ASSERT(size_ > 0);
        memmove(&ptr_[index], &ptr_[index + 1], sizeof(T) * (size_ - (index + 1)));
        size_--;
    }

    template <typename U = T>
    typename enable_if<!is_pod<U>::value, void>::type
    shift_forward(size_t index) {
        MX_DEBUG_ASSERT(size_ > 0);
        for (size_t i = index; (i + 1) < size_; i++) {
            ptr_[i] = mxtl::move(ptr_[i + 1]);
        }
        ptr_[--size_].~T();
    }

    template <typename U = T>
    typename enable_if<is_pod<U>::value, void>::type
    transfer_to(T* newPtr, size_t elements) {
        memcpy(newPtr, ptr_, elements * sizeof(T));
    }

    template <typename U = T>
    typename enable_if<!is_pod<U>::value, void>::type
    transfer_to(T* newPtr, size_t elements) {
        for (size_t i = 0; i < elements; i++) {
            new (&newPtr[i]) T(mxtl::move(ptr_[i]));
            ptr_[i].~T();
        }
    }

    // Grows the vector's capacity to accommodate one more element.
    // Returns true on success, false on failure.
    bool grow_for_new_element() {
        MX_DEBUG_ASSERT(size_ <= capacity_);
        if (size_ == capacity_) {
            size_t newCapacity = capacity_ < kCapacityMinimum ? kCapacityMinimum :
                    capacity_ * kCapacityGrowthFactor;
            if (!reallocate(newCapacity)) {
                return false;
            }
        }
        return true;
    }

    // Shrink the vector to fit a smaller number of elements, if we reach
    // under the shrink factor.
    void consider_shrinking() {
        if (size_ * kCapacityShrinkFactor < capacity_ &&
            capacity_ > kCapacityMinimum) {
            // Try to shrink the underlying storage
            static_assert((kCapacityMinimum + 1) >= kCapacityShrinkFactor,
                          "Capacity heuristics risk reallocating to zero capacity");
            size_t newCapacity = capacity_ / kCapacityShrinkFactor;
            reallocate(newCapacity);
        }
    }

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
        transfer_to(newPtr, size_);
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
