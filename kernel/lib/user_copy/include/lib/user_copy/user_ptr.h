// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <kernel/vm.h>
#include <lib/user_copy.h>
#include <mxtl/type_support.h>

namespace internal {

// A helper that we can specialize, so we can make user_ptr<T>::copy_array_{to,from}_user() work
// even when T is void. (Function specializations have to be in namespace scope, so this can't be
// internal to |user_ptr|.)
template <typename S> inline constexpr size_t type_size() { return sizeof(S); }
template <> inline constexpr size_t type_size<void>() { return 1u; }

}  // namespace internal

// user_ptr<> wraps a pointer to user memory, to differntiate it from kernel
// memory.
template <typename T>
class user_ptr {
public:
    explicit user_ptr(T* const p)
        : ptr_(p) {}

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

    // check that the address is inside user space
    bool is_user_address() const { return ::is_user_address(reinterpret_cast<vaddr_t>(ptr_)); }

    // Copies a single T to user memory. (Using this will fail to compile if T is |void|.)
    // Note: The templatization is simply to allow the class to compile if T is |void|.
    template <typename S = T>
    status_t copy_to_user(const S& src) const {
        static_assert(mxtl::is_same<S, T>::value, "Do not use the template parameter.");
        return copy_to_user_unsafe(ptr_, &src, sizeof(S));
    }

    // Copies an array of T to user memory. Note: This takes a count not a size, unless T is |void|.
    // WARNING: This does not check that |count| is reasonable (i.e., that multiplication won't
    // overflow).
    status_t copy_array_to_user(const T* src, size_t count) const {
        return copy_to_user_unsafe(
                ptr_, src, count * internal::type_size<typename mxtl::remove_volatile<T>::type>());
    }

    // Copies a single T from user memory. (Using this will fail to compile if T is |void|.)
    status_t copy_from_user(typename mxtl::remove_const<T>::type* dst) const {
        // Intentionally use sizeof(T) here, so *using* this method won't compile if T is |void|.
        return copy_from_user_unsafe(dst, ptr_, sizeof(T));
    }

    // Copies an array of T from user memory. Note: This takes a count not a size, unless T is
    // |void|.
    // WARNING: This does not check that |count| is reasonable (i.e., that multiplication won't
    // overflow).
    status_t copy_array_from_user(typename mxtl::remove_const<T>::type* dst, size_t count) const {
        return copy_from_user_unsafe(
                dst, ptr_, count * internal::type_size<typename mxtl::remove_cv<T>::type>());
    }

private:
    // It is very important that this class only wrap the pointer type itself
    // and not include any other members so as not to break the ABI between
    // the kernel and user space.
    T* const ptr_;
};
