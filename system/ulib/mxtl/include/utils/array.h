// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <utils/unique_ptr.h>

namespace mxtl {

template <typename T, typename Deleter = default_delete<T[]>>
class Array {
public:
    constexpr Array() : ptr_(nullptr), count_(0U) {}
    constexpr Array(decltype(nullptr)) : Array() {}

    Array(T* array, size_t count) : ptr_(array), count_(count) {}

    Array(Array&& other) : ptr_(nullptr), count_(other.count_) {
        ptr_ = other.release();
    }

    size_t size() const {
        return count_;
    }

    ~Array() {
        if (ptr_) Deleter()(ptr_);
    }

    Array& operator=(Array&& o) {
        auto count = o.count_;
        reset(o.release(), count);
        return *this;
    }

    Array(const Array& o) = delete;
    Array& operator=(const Array& o) = delete;

    T* release() {
        T* t = ptr_;
        ptr_ = nullptr;
        count_ = 0;
        return t;
    }

    void reset() {
        reset(nullptr, 0U);
    }

    void reset(T* t, size_t count) {
        if (ptr_) Deleter()(ptr_);
        ptr_ = t;
        count_ = count;
    }

    void swap(Array& other) {
        T* t = ptr_;
        ptr_ = other.ptr_;
        other.ptr_ = t;
        size_t c = count_;
        count_ = other.count_;
        other.count_ = c;
    }

    T* get() const {
        return ptr_;
    }

    explicit operator bool() const {
        return static_cast<bool>(ptr_);
    }

    T& operator[](size_t i) const {
        DEBUG_ASSERT(i < count_);
        return ptr_[i];
    }

private:
    T* ptr_;
    size_t count_;
};
}
