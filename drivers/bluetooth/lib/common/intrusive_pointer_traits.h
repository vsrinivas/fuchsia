// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fxl/logging.h"

namespace fbl {
namespace internal {

// ContainerPtrTraits specialization for std::unique_ptr. This allows fbl
// intrusive containers (fbl::DoublyLinkedList and fbl::SinglyLinkedList) to be
// used with std::unique_ptr.
//
// See:
// https://fuchsia.googlesource.com/zircon/+/master/system/ulib/fbl/include/fbl/intrusive_pointer_traits.h
template <typename T>
struct ContainerPtrTraits<::std::unique_ptr<T>> {
  using ValueType = T;
  using RefType = T&;
  using ConstRefType = const T&;
  using PtrType = ::std::unique_ptr<T>;
  using ConstPtrType = ::std::unique_ptr<const T>;
  using RawPtrType = T*;
  using ConstRawPtrType = const T*;

  static constexpr bool IsManaged = true;

  static inline T* GetRaw(const PtrType& ptr) { return ptr.get(); }
  static inline const T* GetRaw(const ConstPtrType& ptr) { return ptr.get(); }
  static inline PtrType Take(PtrType& ptr) { return PtrType(fbl::move(ptr)); }
  static inline void Swap(PtrType& first, PtrType& second) {
    first.swap(second);
  }

  static constexpr PtrType MakeSentinel(void* sentinel) {
    return PtrType(reinterpret_cast<RawPtrType>(
        reinterpret_cast<uintptr_t>(sentinel) | kContainerSentinelBit));
  }

  static inline void DetachSentinel(PtrType& ptr) {
    __UNUSED RawPtrType detached = ptr.release();
    FXL_DCHECK((detached == nullptr) || IsSentinel(detached));
  }

  static constexpr bool IsSentinel(const PtrType& ptr) {
    return IsSentinel(ptr.get());
  }
  static constexpr bool IsSentinel(RawPtrType ptr) {
    return (reinterpret_cast<uintptr_t>(ptr) & kContainerSentinelBit) != 0;
  }

  static constexpr bool IsValid(const PtrType& ptr) {
    return IsValid(ptr.get());
  }
  static constexpr bool IsValid(RawPtrType ptr) {
    return ptr && !IsSentinel(ptr);
  }
};

}  // namespace internal
}  // namespace fbl
