// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <arch/user_copy.h>
#include <kernel/vm.h>
#include <lib/user_copy/internal.h>
#include <magenta/types.h>
#include <fbl/type_support.h>

// user_ptr<> wraps a pointer to user memory, to differentiate it from kernel
// memory.
template <typename T>
class user_ptr {
public:
    explicit user_ptr(T* p) : ptr_(p) {}

    T* get() const { return ptr_; }

    template <typename C>
    user_ptr<C> reinterpret() const { return user_ptr<C>(reinterpret_cast<C*>(ptr_)); }

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
    mx_status_t copy_to_user(const S& src) const {
        static_assert(fbl::is_same<S, T>::value, "Do not use the template parameter.");
        return arch_copy_to_user(ptr_, &src, sizeof(S));
    }

    // Copies an array of T to user memory. Note: This takes a count not a size, unless T is |void|.
    // WARNING: This does not check that |count| is reasonable (i.e., that multiplication won't
    // overflow).
    mx_status_t copy_array_to_user(const T* src, size_t count) const {
        return arch_copy_to_user(ptr_, src, count * internal::type_size<T>());
    }

    // Copies an array of T to user memory. Note: This takes a count not a size, unless T is |void|.
    // WARNING: This does not check that |count| is reasonable (i.e., that multiplication won't
    // overflow).
    mx_status_t copy_array_to_user(const T* src, size_t count, size_t offset) const {
        return arch_copy_to_user(ptr_ + offset, src, count * internal::type_size<T>());
    }

    // Copies a single T from user memory. (Using this will fail to compile if T is |void|.)
    mx_status_t copy_from_user(typename fbl::remove_const<T>::type* dst) const {
        // Intentionally use sizeof(T) here, so *using* this method won't compile if T is |void|.
        return arch_copy_from_user(dst, ptr_, sizeof(T));
    }

    // Copies an array of T from user memory. Note: This takes a count not a size, unless T is
    // |void|.
    // WARNING: This does not check that |count| is reasonable (i.e., that multiplication won't
    // overflow).
    mx_status_t copy_array_from_user(typename fbl::remove_const<T>::type* dst, size_t count) const {
        return arch_copy_from_user(dst, ptr_, count * internal::type_size<T>());
    }

    // Copies a sub-array of T from user memory. Note: This takes a count not a size, unless T is
    // |void|.
    // WARNING: This does not check that |count| is reasonable (i.e., that multiplication won't
    // overflow).
    mx_status_t copy_array_from_user(typename fbl::remove_const<T>::type* dst, size_t count, size_t offset) const {
        return arch_copy_from_user(dst, ptr_ + offset, count * internal::type_size<T>());
    }

private:
    // It is very important that this class only wrap the pointer type itself
    // and not include any other members so as not to break the ABI between
    // the kernel and user space.
    T* const ptr_;
};

template <typename T>
user_ptr<T> make_user_ptr(T* p) { return user_ptr<T>(p); }
