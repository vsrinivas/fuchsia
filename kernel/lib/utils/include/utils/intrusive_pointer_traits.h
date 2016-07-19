// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once
#include <utils/ref_ptr.h>
#include <utils/unique_ptr.h>
#include <utils/type_support.h>

namespace utils {
namespace internal {

// Declaration of the base type which will be used to control what type of
// pointers are permitted to be in containers.
template <typename T> struct ContainerPtrTraits;

// Traits for managing raw pointers.
template <typename T>
struct ContainerPtrTraits<T*> {
    using ValueType       = T;
    using RefType         = T&;
    using ConstRefType    = const T&;
    using PtrType         = T*;
    using ConstPtrType    = const T*;
    using RawPtrType      = T*;
    using ConstRawPtrType = const T*;

    static constexpr bool IsManaged = false;

    static inline T*       GetRaw(const PtrType& ptr) { return ptr; }
    static inline PtrType  Copy(RawPtrType& ptr)      { return PtrType(ptr); }

    static inline PtrType Take(PtrType& ptr) {
        PtrType ret = ptr;
        ptr = nullptr;
        return ret;
    }

    static inline void Swap(PtrType& first, PtrType& second) {
        PtrType tmp = first;
        first = second;
        second = tmp;
    }
};

// Traits for managing unique pointers.
template <typename T>
struct ContainerPtrTraits<::utils::unique_ptr<T>> {
    using ValueType       = T;
    using RefType         = T&;
    using ConstRefType    = const T&;
    using PtrType         = ::utils::unique_ptr<T>;
    using ConstPtrType    = ::utils::unique_ptr<const T>;
    using RawPtrType      = T*;
    using ConstRawPtrType = const T*;

    static constexpr bool IsManaged = true;

    static inline T*       GetRaw(PtrType& ptr)       { return ptr.get(); }
    static inline const T* GetRaw(const PtrType& ptr) { return ptr.get(); }
    static inline PtrType  Take(PtrType& ptr)         { return PtrType(utils::move(ptr)); }
    static inline void     Swap(PtrType& first, PtrType& second) { first.swap(second); }
};

// Traits for managing ref_counted pointers.
template <typename T>
struct ContainerPtrTraits<::utils::RefPtr<T>> {
    using ValueType       = T;
    using RefType         = T&;
    using ConstRefType    = const T&;
    using PtrType         = ::utils::RefPtr<T>;
    using ConstPtrType    = ::utils::RefPtr<const T>;
    using RawPtrType      = T*;
    using ConstRawPtrType = const T*;

    static constexpr bool IsManaged = true;

    static inline T*       GetRaw(const PtrType& ptr) { return ptr.get(); }
    static inline PtrType  Copy(RawPtrType& ptr)      { return PtrType(ptr); }
    static inline PtrType  Take(PtrType& ptr)         { return PtrType(utils::move(ptr)); }
    static inline void     Swap(PtrType& first, PtrType& second) { first.swap(second); }

};

}  // namespace internal
}  // namespace utils
