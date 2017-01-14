// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <mxtl/deleter.h>
#include <mxtl/macros.h>
#include <mxtl/type_support.h>

namespace mxtl {

// This is a simplified version of std::unique_ptr that supports custom
// stateless deleters but doesn't support type conversions between different
// pointer types.
template <typename T, typename Deleter = default_delete<T>>
class unique_ptr {
public:
    constexpr unique_ptr() : ptr_(nullptr) {}
    constexpr unique_ptr(decltype(nullptr)) : unique_ptr() {}

    explicit unique_ptr(T* t) : ptr_(t) { }

    ~unique_ptr() {
        if (ptr_) Deleter()(ptr_);
    }

    unique_ptr(unique_ptr&& o) : ptr_(o.release()) {}
    unique_ptr& operator=(unique_ptr&& o) {
        reset(o.release());
        return *this;
    }

    unique_ptr& operator=(decltype(nullptr)) {
        reset();
        return *this;
    }

    // Comparison against nullptr operators (of the form, myptr == nullptr).
    bool operator==(decltype(nullptr)) const { return (ptr_ == nullptr); }
    bool operator!=(decltype(nullptr)) const { return (ptr_ != nullptr); }

    // Comparison against other unique_ptr<>'s.
    bool operator==(const unique_ptr& o) const { return ptr_ == o.ptr_; }
    bool operator!=(const unique_ptr& o) const { return ptr_ != o.ptr_; }
    bool operator< (const unique_ptr& o) const { return ptr_ <  o.ptr_; }
    bool operator<=(const unique_ptr& o) const { return ptr_ <= o.ptr_; }
    bool operator> (const unique_ptr& o) const { return ptr_ >  o.ptr_; }
    bool operator>=(const unique_ptr& o) const { return ptr_ >= o.ptr_; }

    // move semantics only
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(unique_ptr);

    T* release() {
        T* t = ptr_;
        ptr_ = nullptr;
        return t;
    }
    void reset(T* t = nullptr) {
        if (ptr_) Deleter()(ptr_);
        ptr_ = t;
    }
    void swap(unique_ptr& other) {
        T* t = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = t;
    }

    T* get() const {
        return ptr_;
    }

    explicit operator bool() const {
        return static_cast<bool>(ptr_);
    }

    T& operator*() const {
        return *ptr_;
    }
    T* operator->() const {
        return ptr_;
    }

    // Implicit upcasting via construction.
    //
    // We permit implicit casting of a unique_ptr<U> to a unique_ptr<T> as long
    // as...
    //
    // 1) U* is implicitly convertible to a T*
    // 2) Neither T nor U are a class/struct type, or both T and U are
    //    class/struct types, and T has a virtual destructor.
    // 3) The unique_ptr<U> we are casting from is using mxtl::default_delete as
    //    its deleter.
    // 4) The unique_ptr<T> we are casting to is using mxtl::default_delete as
    //    its deleter.
    //
    // Note: we do this via an implicit converting constructor (instead of a
    // user-defined conversion operator) so that we can demand that we are
    // converting from a properly moved rvalue reference.
    //
    // Also Note:  This behavior is *not* the same as std::unique_ptr.
    // std::unique_ptr only cares about point #1.  Its behavior emulates raw
    // pointers and will gladly let you implicitly convert a class U to a class
    // T as an implicit upcast regardless of whether or not T has a virtual
    // destructor.
    template <typename U, typename U_Deleter,
              typename = typename enable_if<is_convertible_pointer<U*, T*>::value>::type>
    unique_ptr(unique_ptr<U, U_Deleter>&& o) : ptr_(o.release()) {
        static_assert((is_class<T>::value == is_class<U>::value) &&
                     (!is_class<T>::value || has_virtual_destructor<T>::value),
                "Cannot convert unique_ptr<U> to unique_ptr<T> unless neither T "
                "nor U are class/struct types, or T has a virtual destructor");

        static_assert(is_same<U_Deleter, default_delete<U>>::value,
                "Cannot convert unique_ptr<U> to unique_ptr<T, ...> unless unique_ptr<U, ...> is "
                "using default_delete<U>.");

        static_assert(is_same<Deleter, default_delete<T>>::value,
                "Cannot convert unique_ptr<U> to unique_ptr<T, ...> unless unique_ptr<T, ...> is "
                "using default_delete<T>.");
    }

private:
    T* ptr_;
};

template <typename T, typename Deleter>
class unique_ptr<T[], Deleter> {
public:
    constexpr unique_ptr() : ptr_(nullptr) {}
    constexpr unique_ptr(decltype(nullptr)) : unique_ptr() {}

    explicit unique_ptr(T* array) : ptr_(array) {}

    unique_ptr(unique_ptr&& other) : ptr_(other.release()) {}

    ~unique_ptr() {
        if (ptr_) Deleter()(ptr_);
    }

    unique_ptr& operator=(unique_ptr&& o) {
        reset(o.release());
        return *this;
    }

    // Comparison against nullptr operators (of the form, myptr == nullptr).
    bool operator==(decltype(nullptr)) const { return (ptr_ == nullptr); }
    bool operator!=(decltype(nullptr)) const { return (ptr_ != nullptr); }

    // Comparison against other unique_ptr<>'s.
    bool operator==(const unique_ptr& o) const { return ptr_ == o.ptr_; }
    bool operator!=(const unique_ptr& o) const { return ptr_ != o.ptr_; }
    bool operator< (const unique_ptr& o) const { return ptr_ <  o.ptr_; }
    bool operator<=(const unique_ptr& o) const { return ptr_ <= o.ptr_; }
    bool operator> (const unique_ptr& o) const { return ptr_ >  o.ptr_; }
    bool operator>=(const unique_ptr& o) const { return ptr_ >= o.ptr_; }

    unique_ptr(const unique_ptr& o) = delete;
    unique_ptr& operator=(const unique_ptr& o) = delete;

    T* release() {
        T* t = ptr_;
        ptr_ = nullptr;
        return t;
    }
    void reset(T* t = nullptr) {
        if (ptr_) Deleter()(ptr_);
        ptr_ = t;
    }
    void swap(unique_ptr& other) {
        T* t = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = t;
    }

    T* get() const {
        return ptr_;
    }

    explicit operator bool() const {
        return static_cast<bool>(ptr_);
    }
    T& operator[](size_t i) const {
        return ptr_[i];
    }

private:
    T* ptr_;
};

// Comparison against nullptr operators (of the form, nullptr == myptr) for T and T[]
template <typename T, typename Deleter>
static inline bool operator==(decltype(nullptr), const unique_ptr<T, Deleter>& ptr) {
    return (ptr.get() == nullptr);
}

template <typename T, typename Deleter>
static inline bool operator!=(decltype(nullptr), const unique_ptr<T, Deleter>& ptr) {
    return (ptr.get() != nullptr);
}

}  // namespace mxtl
