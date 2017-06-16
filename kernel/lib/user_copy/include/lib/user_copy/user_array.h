// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stddef.h>
#include <kernel/vm.h>
#include <lib/user_copy.h>
#include <lib/user_copy/internal.h>
#include <lib/user_copy/user_ptr.h>
#include <mxtl/type_support.h>

// user_array<> provides safe access to user memory for arrays.
template <typename T>
class user_array {
public:
    // TODO(vtl): Neither this nor init_count() check that |count| is plausible; |count * sizeof(T)|
    // better not overflow.
    explicit user_array(T* ptr, size_t count = 0u) : ptr_(ptr), count_(count) {}

    void init_count(size_t count) { count_ = count; }

    // Gets a raw pointer (used mainly for logging).
    T* get_unsafe() const { return ptr_; }

    // Gets a user_ptr. TODO(vtl): Maybe we can get rid of this?
    user_ptr<T> get_user_ptr() const { return user_ptr<T>(ptr_); }

    size_t count() const { return count_; }
    size_t size() const { return count_ * internal::type_size<T>(); }

    // TODO(vtl): Do we need a reinterpret() method of some type?

    // Tests as true if the pointer is non-null.
    explicit operator bool() const { return ptr_ != nullptr; }

    // Returns a user_array to the subarray starting at the |index|-th element. The pointer must be
    // non-null, the array's size must be initialized, and |index| must be strictly less than
    // |count()|. (Using this will fail to compile if T is |void|.)
    user_array element_offset(size_t index) const {
        DEBUG_ASSERT(ptr_ && count_ && index < count_);
        return user_array(ptr_ + index, count_ - index);
    }

    // Returns a user_array<[const] void> offset by |offset| bytes from this one (with the
    // appropriate size).
    user_array<typename mxtl::match_cv<T, void>::type> byte_offset(size_t offset) const {
        DEBUG_ASSERT(ptr_ && count_ && offset < size());
        return user_array<typename mxtl::match_cv<T, void>::type>(
                reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr_) + offset), size() - offset);
    }

    // Check that the address is inside user space.
    // TODO(vtl): Make this more robust -- this only checks the initial address, not the full range
    // of the array.
    bool is_user_address() const { return ::is_user_address(reinterpret_cast<vaddr_t>(ptr_)); }

    // Copies |count| T's (or bytes, if T is void) to user memory. If |count| is too large, this
    // fails with MX_ERR_BUFFER_TOO_SMALL.
    status_t copy_to_user(const T* src, size_t count) const {
        if (count > count_)
            return MX_ERR_BUFFER_TOO_SMALL;
        return copy_to_user_unsafe(ptr_, src, count * internal::type_size<T>());
    }

    // Copies |count| T's (or bytes, if T is void) from user memory. If |count| is too large, this
    // fails with MX_ERR_BUFFER_TOO_SMALL.
    status_t copy_from_user(typename mxtl::remove_const<T>::type* dst, size_t count) {
        if (count > count_)
            return MX_ERR_BUFFER_TOO_SMALL;
        return copy_from_user_unsafe(dst, ptr_, count * internal::type_size<T>());
    }

private:
    T* const ptr_;
    size_t count_;
};
