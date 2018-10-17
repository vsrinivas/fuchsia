// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_INTRUSIVE_POINTER_TRAITS_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_INTRUSIVE_POINTER_TRAITS_H_

#include <zircon/assert.h>

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
  static constexpr bool CanCopy = false;

  static inline T* GetRaw(const PtrType& ptr) { return ptr.get(); }

  static inline RawPtrType Leak(PtrType& ptr) __WARN_UNUSED_RESULT {
    return ptr.release();
  }

  static inline PtrType Reclaim(RawPtrType ptr) { return PtrType(ptr); }
};

}  // namespace internal
}  // namespace fbl

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_COMMON_INTRUSIVE_POINTER_TRAITS_H_
