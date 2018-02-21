// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <arch/user_copy.h>
#include <fbl/type_support.h>
#include <lib/user_copy/internal.h>
#include <zircon/types.h>
#include <vm/vm.h>

// user_*_ptr<> wraps a pointer to user memory, to differentiate it from kernel
// memory. They can be in, out, or inout pointers.

namespace internal {

enum InOutPolicy {
    kIn = 1,
    kOut = 2,
    kInOut = kIn | kOut,
};

template <typename T, InOutPolicy Policy>
class user_ptr {
public:
    static_assert(fbl::is_const<T>::value == (Policy == kIn),
                  "In pointers must be const, and Out and InOut pointers must not be const");

    explicit user_ptr(T* p) : ptr_(p) {}

    user_ptr(const user_ptr& other) : ptr_(other.ptr_) {}

    user_ptr& operator=(const user_ptr& other) {
        ptr_ = other.ptr_;
        return *this;
    }

    T* get() const { return ptr_; }

    template <typename C>
    user_ptr<C, Policy> reinterpret() const { return user_ptr<C, Policy>(reinterpret_cast<C*>(ptr_)); }

    // special operator to return the nullness of the pointer
    explicit operator bool() const { return ptr_ != nullptr; }

    // Returns a user_ptr pointing to the |index|-th element from this one, or a null user_ptr if
    // this pointer is null. Note: This does no other validation, and the behavior is undefined on
    // overflow. (Using this will fail to compile if T is |void|.)
    user_ptr element_offset(size_t index) const {
        return ptr_ ? user_ptr(ptr_ + index) : user_ptr(nullptr);
    }

    // Returns a user_ptr offset by |offset| bytes from this one.
    user_ptr byte_offset(size_t offset) const {
        return ptr_ ? user_ptr(reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(ptr_) + offset))
                    : user_ptr(nullptr);
    }

    // Copies a single T to user memory. (Using this will fail to compile if T is |void|.)
    // Note: The templatization is simply to allow the class to compile if T is |void|.
    template <typename S = T>
    zx_status_t copy_to_user(const S& src) const {
        static_assert(fbl::is_same<S, T>::value, "Do not use the template parameter.");
        static_assert(Policy & kOut, "can only copy to user for kOut or kInOut user_ptr");
        return arch_copy_to_user(ptr_, &src, sizeof(S));
    }

    // Copies an array of T to user memory. Note: This takes a count not a size, unless T is |void|.
    zx_status_t copy_array_to_user(const T* src, size_t count) const {
        static_assert(Policy & kOut, "can only copy to user for kOut or kInOut user_ptr");
        size_t len;
        if (mul_overflow(count, internal::type_size<T>(), &len)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return arch_copy_to_user(ptr_, src, len);
    }

    // Copies an array of T to user memory. Note: This takes a count not a size, unless T is |void|.
    zx_status_t copy_array_to_user(const T* src, size_t count, size_t offset) const {
        static_assert(Policy & kOut, "can only copy to user for kOut or kInOut user_ptr");
        size_t len;
        if (mul_overflow(count, internal::type_size<T>(), &len)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return arch_copy_to_user(ptr_ + offset, src, len);
    }

    // Copies a single T from user memory. (Using this will fail to compile if T is |void|.)
    zx_status_t copy_from_user(typename fbl::remove_const<T>::type* dst) const {
        static_assert(Policy & kIn, "can only copy from user for kIn or kInOut user_ptr");
        // Intentionally use sizeof(T) here, so *using* this method won't compile if T is |void|.
        return arch_copy_from_user(dst, ptr_, sizeof(T));
    }

    // Copies an array of T from user memory. Note: This takes a count not a size, unless T is
    // |void|.
    zx_status_t copy_array_from_user(typename fbl::remove_const<T>::type* dst, size_t count) const {
        static_assert(Policy & kIn, "can only copy from user for kIn or kInOut user_ptr");
        size_t len;
        if (mul_overflow(count, internal::type_size<T>(), &len)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return arch_copy_from_user(dst, ptr_, len);
    }

    // Copies a sub-array of T from user memory. Note: This takes a count not a size, unless T is
    // |void|.
    zx_status_t copy_array_from_user(typename fbl::remove_const<T>::type* dst, size_t count, size_t offset) const {
        static_assert(Policy & kIn, "can only copy from user for kIn or kInOut user_ptr");
        size_t len;
        if (mul_overflow(count, internal::type_size<T>(), &len)) {
            return ZX_ERR_INVALID_ARGS;
        }
        return arch_copy_from_user(dst, ptr_ + offset, len);
    }

private:
    // It is very important that this class only wrap the pointer type itself
    // and not include any other members so as not to break the ABI between
    // the kernel and user space.
    T* ptr_;
};

} // namespace internal

template <typename T>
using user_in_ptr = internal::user_ptr<T, internal::kIn>;

template <typename T>
using user_out_ptr = internal::user_ptr<T, internal::kOut>;

template <typename T>
using user_inout_ptr = internal::user_ptr<T, internal::kInOut>;

template <typename T>
user_in_ptr<T> make_user_in_ptr(T* p) { return user_in_ptr<T>(p); }

template <typename T>
user_out_ptr<T> make_user_out_ptr(T* p) { return user_out_ptr<T>(p); }

template <typename T>
user_inout_ptr<T> make_user_inout_ptr(T* p) { return user_inout_ptr<T>(p); }
