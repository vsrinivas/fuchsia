// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <kernel/vm.h>
#include <lib/user_copy.h>

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

    // allow size_t based addition on the pointer
    user_ptr operator+(size_t add) const {
        if (ptr_ == nullptr)
            return user_ptr(nullptr);

        auto ptr = reinterpret_cast<uintptr_t>(ptr_);
        return user_ptr(reinterpret_cast<T*>(ptr + add));
    }

    // check that the address is inside user space
    bool is_user_address() const { return ::is_user_address(reinterpret_cast<vaddr_t>(ptr_)); }

private:
    // It is very important that this class only wrap the pointer type itself
    // and not include any other members so as not to break the ABI between
    // the kernel and user space.
    T* const ptr_;
};

// TODO(vtl): Add appropriate methods to user_ptr, and probably get rid of the functions below.

template <typename T>
inline status_t copy_to_user(user_ptr<T> dst, const void* src, size_t len) {
  return copy_to_user_unsafe(dst.get(), src, len);
}
template <typename T>
inline status_t copy_from_user(void* dst, const user_ptr<T> src, size_t len) {
  return copy_from_user_unsafe(dst, src.get(), len);
}

// Convenience functions for common data types, this time with more C++.
#define MAKE_COPY_TO_USER(name, type) \
    static inline status_t name(user_ptr<type> dst, type value) { \
        return copy_to_user(dst, &value, sizeof(type)); \
    }

MAKE_COPY_TO_USER(copy_to_user_u8, uint8_t);
MAKE_COPY_TO_USER(copy_to_user_u16, uint16_t);
MAKE_COPY_TO_USER(copy_to_user_32, int32_t);
MAKE_COPY_TO_USER(copy_to_user_u32, uint32_t);
MAKE_COPY_TO_USER(copy_to_user_u64, uint64_t);
MAKE_COPY_TO_USER(copy_to_user_uptr, uintptr_t);
MAKE_COPY_TO_USER(copy_to_user_iptr, intptr_t);

#undef MAKE_COPY_TO_USER

#define MAKE_COPY_FROM_USER(name, type) \
    static inline status_t name(type *dst, const user_ptr<const type> src) { \
        return copy_from_user(dst, src, sizeof(type)); \
    } \
    static inline status_t name(type *dst, const user_ptr<type> src) { \
        return copy_from_user(dst, src, sizeof(type)); \
    }

MAKE_COPY_FROM_USER(copy_from_user_u8, uint8_t);
MAKE_COPY_FROM_USER(copy_from_user_u16, uint16_t);
MAKE_COPY_FROM_USER(copy_from_user_u32, uint32_t);
MAKE_COPY_FROM_USER(copy_from_user_32, int32_t);
MAKE_COPY_FROM_USER(copy_from_user_u64, uint64_t);
MAKE_COPY_FROM_USER(copy_from_user_uptr, uintptr_t);
MAKE_COPY_FROM_USER(copy_from_user_iptr, intptr_t);

#undef MAKE_COPY_FROM_USER
