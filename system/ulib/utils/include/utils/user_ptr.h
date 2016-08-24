// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#if __cplusplus

namespace utils {

// user_ptr<> wraps a pointer to user memory, to differntiate it from kernel
// memory.
template <typename T>
class user_ptr {
public:
    user_ptr() : ptr_(nullptr) {}
    explicit user_ptr(T* const p) : ptr_(p) {}

    T* get() { return ptr_; }
    T* get() const { return ptr_; }

    template <typename C>
    user_ptr<C> reinterpret() const { return user_ptr<C>(reinterpret_cast<C*>(ptr_)); }

    operator bool() const { return ptr_ != nullptr; }
    bool operator!() { return ptr_ == nullptr; }

private:
    // It is very important that this class only wrap the pointer type itself
    // and not include any other members so as not to break the ABI between
    // the kernel and user space.
    T* const ptr_;
};

}  // namespace utils

#endif  // __cplusplus
