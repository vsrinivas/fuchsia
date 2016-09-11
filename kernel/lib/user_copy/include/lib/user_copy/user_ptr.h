// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <kernel/vm.h>

#if __cplusplus

namespace mxtl {

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

} // namespace mxtl

#endif // __cplusplus
