// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdlib.h>
#include <mxtl/macros.h>
#include <mxtl/type_support.h>

namespace mxtl {

template <typename T>
struct default_delete {
    inline void operator()(T* ptr) const {
        enum { type_must_be_complete = sizeof(T) };
        delete ptr;
    }
};

template <typename T>
struct default_delete<T[]> {
    inline void operator()(T* ptr) const {
        enum { type_must_be_complete = sizeof(T) };
        delete[] ptr;
    }

private:
    // Disable this whenever U != T as C++14 and before do. See
    // http://cplusplus.github.io/LWG/lwg-defects.html#938 for motivation.
    // C++17 has more complex rules for when U(*)[] is implicitly convertible to
    // T(*)[] but we don't do that.
    template <typename U>
    void operator()(U* ptr) const = delete;
};

template <typename T, size_t n>
struct default_delete<T[n]> {
    // Disallow things like unique_ptr<int[10]>.
    static_assert(sizeof(T) == -1, "do not use array with size as type in unique_ptr<>");
};

// Deleter that invokes 'free' on its parameter. Can be used to store
// malloc-allocated pointers as follows:
//
//   unique_ptr<int, free_delete> foo(static_cast<int*>(malloc(sizeof(int))));
struct free_delete {
    inline void operator()(void* ptr) const {
        ::free(ptr);
    }
};

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
