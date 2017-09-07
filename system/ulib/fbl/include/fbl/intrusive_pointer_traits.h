// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <magenta/compiler.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <fbl/type_support.h>

namespace fbl {
namespace internal {

// Notes on container sentinels:
//
// Intrusive container implementations employ a Slightly Evil(tm) pattern where
// sentinel values are used in place of nullptr in various places in the
// internal data structure in order to make some operations a bit easier.
// Generally speaking, a sentinel pointer is a pointer to a container with the
// sentinel bit set.  It is cast and stored in the container's data structure as
// a pointer to an element which the container contains, even though it is
// actually a slightly damaged pointer to the container itself.
//
// An example of where this is used is in the doubly linked list implementation.
// The final element in the list holds the container's sentinel value instead of
// nullptr or a pointer to the head of the list.  When an iterator hits the end
// of the list, it knows that it is at the end (because the sentinel bit is set)
// but can still get back to the list itself (by clearing the sentinel bit in
// the pointer) without needing to store an explicit pointer to the list itself.
//
// Care must be taken when storing sentinel values in managed pointer types.
// They are *not* valid pointers, and it is very important that unique_ptr and
// RefPtr make no attempt to delete, AddRef, Release, or Adopt a sentinel
// pointer.  In addition, it is essential that a legitimate pointer to a
// container never need to set the sentinel bit.  Currently, bit 0 is being used
// because it should never be possible to have a proper container instance which
// is odd-aligned.
constexpr uintptr_t kContainerSentinelBit = 1u;

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
    static constexpr bool CanCopy = true;

    static inline T*       GetRaw(const ConstPtrType& ptr) { return const_cast<RawPtrType>(ptr); }
    static inline PtrType  Copy(RawPtrType& ptr)           { return PtrType(ptr); }

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

    static constexpr PtrType MakeSentinel(void* sentinel) {
        return reinterpret_cast<RawPtrType>(reinterpret_cast<uintptr_t>(sentinel) |
                                            kContainerSentinelBit);
    }

    static inline void DetachSentinel(PtrType& ptr) {
        MX_DEBUG_ASSERT((ptr == nullptr) || IsSentinel(ptr));
        ptr = nullptr;
    }

    static constexpr bool IsSentinel(const PtrType& ptr) {
        return (reinterpret_cast<uintptr_t>(ptr) & kContainerSentinelBit) != 0;
    }

    static constexpr bool IsValid(const PtrType& ptr) { return ptr && !IsSentinel(ptr); }
};

// Traits for managing unique pointers.
template <typename T>
struct ContainerPtrTraits<::fbl::unique_ptr<T>> {
    using ValueType       = T;
    using RefType         = T&;
    using ConstRefType    = const T&;
    using PtrType         = ::fbl::unique_ptr<T>;
    using ConstPtrType    = ::fbl::unique_ptr<const T>;
    using RawPtrType      = T*;
    using ConstRawPtrType = const T*;

    static constexpr bool IsManaged = true;
    static constexpr bool CanCopy = false;

    static inline T*       GetRaw(const PtrType& ptr)      { return ptr.get(); }
    static inline const T* GetRaw(const ConstPtrType& ptr) { return ptr.get(); }
    static inline PtrType  Take(PtrType& ptr)              { return PtrType(fbl::move(ptr)); }
    static inline void     Swap(PtrType& first, PtrType& second) { first.swap(second); }

    static constexpr PtrType MakeSentinel(void* sentinel) {
        return PtrType(reinterpret_cast<RawPtrType>(reinterpret_cast<uintptr_t>(sentinel) |
                                                    kContainerSentinelBit));
    }

    static inline void DetachSentinel(PtrType& ptr) {
        __UNUSED RawPtrType detached = ptr.release();
        MX_DEBUG_ASSERT((detached == nullptr) || IsSentinel(detached));
    }

    static constexpr bool IsSentinel(const PtrType& ptr) { return IsSentinel(ptr.get()); }
    static constexpr bool IsSentinel(RawPtrType ptr) {
        return (reinterpret_cast<uintptr_t>(ptr) & kContainerSentinelBit) != 0;
    }

    static constexpr bool IsValid(const PtrType& ptr) { return IsValid(ptr.get()); }
    static constexpr bool IsValid(RawPtrType ptr)     { return ptr && !IsSentinel(ptr); }
};

// Traits for managing ref_counted pointers.
template <typename T>
struct ContainerPtrTraits<::fbl::RefPtr<T>> {
    using ValueType       = T;
    using RefType         = T&;
    using ConstRefType    = const T&;
    using PtrType         = ::fbl::RefPtr<T>;
    using ConstPtrType    = ::fbl::RefPtr<const T>;
    using RawPtrType      = T*;
    using ConstRawPtrType = const T*;

    static constexpr bool IsManaged = true;
    static constexpr bool CanCopy = true;

    static inline T*       GetRaw(const PtrType& ptr)      { return ptr.get(); }
    static inline const T* GetRaw(const ConstPtrType& ptr) { return ptr.get(); }
    static inline PtrType  Copy(RawPtrType& ptr)           { return PtrType(ptr); }
    static inline PtrType  Take(PtrType& ptr)              { return PtrType(fbl::move(ptr)); }
    static inline void     Swap(PtrType& first, PtrType& second) { first.swap(second); }

    static constexpr PtrType MakeSentinel(void* sentinel) {
        return ::fbl::internal::MakeRefPtrNoAdopt(reinterpret_cast<RawPtrType>(
                    reinterpret_cast<uintptr_t>(sentinel) | kContainerSentinelBit));
    }

    static inline void DetachSentinel(PtrType& ptr) {
        __UNUSED RawPtrType detached = ptr.leak_ref();
        MX_DEBUG_ASSERT((detached == nullptr) || IsSentinel(detached));
    }

    static constexpr bool IsSentinel(const PtrType& ptr) { return IsSentinel(ptr.get()); }
    static constexpr bool IsSentinel(RawPtrType ptr) {
        return (reinterpret_cast<uintptr_t>(ptr) & kContainerSentinelBit) != 0;
    }

    static constexpr bool IsValid(const PtrType& ptr) { return IsValid(ptr.get()); }
    static constexpr bool IsValid(RawPtrType ptr)     { return ptr && !IsSentinel(ptr); }
};

}  // namespace internal
}  // namespace fbl
