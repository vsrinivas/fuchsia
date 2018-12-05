// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/alloc_checker.h>
#include <fbl/intrusive_pointer_traits.h>
#include <fbl/recycler.h>
#include <memory>

namespace ktl {

namespace internal {

template <typename T>
struct deleter {
    constexpr deleter() noexcept = default;

    template <typename U>
    deleter(const deleter<U>&,
            typename std::enable_if_t<std::is_convertible<U*, T*>::value>* = nullptr)
        noexcept {
    }

    void operator()(T* ptr) const noexcept {
        if constexpr (fbl::internal::has_fbl_recycle<T>::value) {
            fbl::internal::recycler<T>::recycle(ptr);
        } else {
            delete ptr;
        }
    }
};

template <typename T>
struct deleter<T[]> {
    constexpr deleter() noexcept = default;

    void operator()(T* ptr) const noexcept {
        delete[] ptr;
    }
};

} // namespace internal

template <typename T>
using unique_ptr = std::unique_ptr<T, internal::deleter<T>>;

template <typename T, typename... Args>
unique_ptr<T> make_unique(fbl::AllocChecker* ac, Args&&... args) {
    return unique_ptr<T>(new (ac) T(std::forward<Args>(args)...));
}

} // namespace ktl

namespace fbl {
namespace internal {

// Traits for managing unique pointers.
template <typename T>
struct ContainerPtrTraits<::ktl::unique_ptr<T>> {
    using ValueType       = T;
    using RefType         = T&;
    using ConstRefType    = const T&;
    using PtrType         = ktl::unique_ptr<T>;
    using ConstPtrType    = ktl::unique_ptr<const T>;
    using RawPtrType      = T*;
    using ConstRawPtrType = const T*;

    static constexpr bool IsManaged = true;
    static constexpr bool CanCopy = false;

    static inline T* GetRaw(const PtrType& ptr) { return ptr.get(); }

    static inline RawPtrType Leak(PtrType& ptr) __WARN_UNUSED_RESULT {
        return ptr.release();
    }

    static inline PtrType Reclaim(RawPtrType ptr) {
        return PtrType(ptr);
    }
};

}  // namespace internal
}  // namespace fbl
