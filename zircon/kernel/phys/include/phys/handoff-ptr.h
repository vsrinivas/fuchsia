// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_HANDOFF_PTR_H_
#define ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_HANDOFF_PTR_H_

#include <stdint.h>

#include <ktl/algorithm.h>
#include <ktl/span.h>
#include <ktl/string_view.h>

// PhysHandoffPtr provides a "smart pointer" style API for pointers handed off
// from physboot to the kernel proper.  A handoff pointer is only ever created
// in physboot by the HandoffPrep class.  It's only ever dereferenced (or
// converted into a raw pointer) in the kernel proper.
//
// Lifetime issues for handoff data are complex.  PhysHandoffPtr is always
// treated as a traditional "owning" smart pointer and is a move-only type.
// Ordinarily, handoff pointer objects will be left in place and only have raw
// pointers extracted from them for later use.

// PhysHandoffPtr has no destructor and the "owning" pointer dying doesn't have
// any direct effect.  The lifetime of all handoff pointers is actually grouped
// in two buckets:
//
//  * Permanent handoff data will be accessible in the kernel's virtual address
//    space permanently.  This data resides on pages that the PMM has been told
//    are owned by kernel mappings.
//
//  * Temporary handoff data must be consumed only during the handoff phase,
//    roughly until the kernel becomes multi-threaded(*).  This data resides on
//    pages that the PMM may be told to reuse after handoff.
//
// (*) TODO(fxbug.dev/84107): Currently permanent handoff data is not possible
// at all, since the kernel proper will consume all physical memory.  Temporary
// data is in fact available until userboot, which is the last thing in kernel
// startup.  In future, the cutoff when temporary handoff pointers become
// invalid will be somewhere after PMM setup TBD.
enum class PhysHandoffPtrLifetime { Permanent, Temporary };

// The handoff pointers can be encoded in two ways.
//
// TODO(fxbug.dev/84107): Currently pointers are physical addresses residing
// inside the data ZBI.  The kernel will access them via the physmap.
// Eventually they will be kernel virtual pointers into some kernel mapping
// (possibly the physmap or possibly dedicated mappings).  The distinction here
// can probably go away and have only kernel virtual pointers be supported.
enum class PhysHandoffPtrEncoding {
  PhysAddr,          // Stored as uintptr_t, physical address.
  KernelVirtualPtr,  // Stored as T*, kernel virtual address.
};

// This is defined only in the kernel proper and not in physboot.
template <PhysHandoffPtrEncoding Encoding>
[[gnu::const]] void* PhysHandoffPtrImportPhysAddr(uintptr_t ptr);

template <typename T, PhysHandoffPtrEncoding Encoding>
struct PhysHandoffPtrTraits {};

template <typename T>
struct PhysHandoffPtrTraits<T, PhysHandoffPtrEncoding::PhysAddr> {
  using ExportType = uintptr_t;

  static T* Import(ExportType ptr) {
    return static_cast<T*>(PhysHandoffPtrImportPhysAddr<PhysHandoffPtrEncoding::PhysAddr>(ptr));
  }
};

#ifdef _KERNEL
template <typename T>
struct PhysHandoffPtrTraits<T, PhysHandoffPtrEncoding::KernelVirtualPtr> {
  using ExportType = T*;

  static_assert(sizeof(ExportType) == sizeof(uintptr_t));
  static_assert(alignof(ExportType) == alignof(uintptr_t));

  static T* Import(ExportType ptr) { return ptr; }
};
#endif

template <typename T, PhysHandoffPtrEncoding Encoding, PhysHandoffPtrLifetime Lifetime>
class PhysHandoffPtr {
 public:
  constexpr PhysHandoffPtr() = default;

  constexpr PhysHandoffPtr(const PhysHandoffPtr&) = delete;

  constexpr PhysHandoffPtr(PhysHandoffPtr&& other) noexcept : ptr_(ktl::exchange(other.ptr_, {})) {}

  constexpr PhysHandoffPtr& operator=(PhysHandoffPtr&& other) noexcept {
    ptr_ = ktl::exchange(other.ptr_, {});
    return *this;
  }

#ifdef _KERNEL
  // Handoff pointers can only be dereferenced in the kernel proper.

  T* get() const { return Traits::Import(ptr_); }

  T* release() { return Traits::Import(ktl::exchange(ptr_, {})); }

  T& operator*() const { return *get(); }

  T* operator->() const { return get(); }
#endif  // _KERNEL

 private:
  using Traits = PhysHandoffPtrTraits<T, Encoding>;

  friend class HandoffPrep;

  typename Traits::ExportType ptr_{};
};

// PhysHandoffSpan<T> is to ktl::span<T> as PhysHandoffPtr<T> is to T*.
// It has get() and release() methods that return ktl::span<T>.

template <typename T, PhysHandoffPtrEncoding Encoding, PhysHandoffPtrLifetime Lifetime>
class PhysHandoffSpan {
 public:
  PhysHandoffSpan() = default;
  PhysHandoffSpan(const PhysHandoffSpan&) = default;

  PhysHandoffSpan(PhysHandoffPtr<T, Encoding, Lifetime> ptr, size_t size)
      : ptr_(ptr), size_(size) {}

#ifdef _KERNEL
  ktl::span<T> get() const { return {ptr_.get(), size_}; }

  ktl::span<T> release() { return {ptr_.release(), size_}; }
#endif

 private:
  friend class HandoffPrep;

  PhysHandoffPtr<T, Encoding, Lifetime> ptr_;
  size_t size_ = 0;
};

// PhysHandoffString is stored just the same as PhysHandoffSpan<const char>,
// but its get() and release() methods yield ktl::string_view.
template <PhysHandoffPtrEncoding Encoding, PhysHandoffPtrLifetime Lifetime>
class PhysHandoffString : public PhysHandoffSpan<const char, Encoding, Lifetime> {
 public:
  using Base = PhysHandoffSpan<const char, Encoding, Lifetime>;

  PhysHandoffString() = default;
  PhysHandoffString(const PhysHandoffString&) = default;

#ifdef _KERNEL
  ktl::string_view get() const {
    ktl::span str = Base::get();
    return {str.data(), str.size()};
  }

  ktl::string_view release() {
    ktl::span str = Base::release();
    return {str.data(), str.size()};
  }
#endif
};

// Convenience aliases used in the PhysHandoff declaration.

template <typename T>
using PhysHandoffTemporaryPtr =
    PhysHandoffPtr<T, PhysHandoffPtrEncoding::PhysAddr, PhysHandoffPtrLifetime::Temporary>;

template <typename T>
using PhysHandoffTemporarySpan =
    PhysHandoffSpan<T, PhysHandoffPtrEncoding::PhysAddr, PhysHandoffPtrLifetime::Temporary>;

using PhysHandoffTemporaryString =
    PhysHandoffString<PhysHandoffPtrEncoding::PhysAddr, PhysHandoffPtrLifetime::Temporary>;

// TODO(fxbug.dev/84107): permanent handoff pointers are not yet available
// template <typename T>
// using PhysHandoffPermanentPtr =
//     PhysHandoffPtr<T, PhysHandoffPtrEncoding::KernelVirtualPtr,
//     PhysHandoffPtrLifetime::Permanent>;
//
// template <typename T>
// using PhysHandoffPermanentSpan =
//     PhysHandoffSpan<T, PhysHandoffPtrEncoding::KernelVirtualPtr,
//     PhysHandoffPtrLifetime::Permanent>;
//
// using PhysHandoffPermanentString =
//    PhysHandoffString<PhysHandoffPtrEncoding::KernelVirtualPtr,
//    PhysHandoffPtrLifetime::Permanent>;

#endif  // ZIRCON_KERNEL_PHYS_INCLUDE_PHYS_HANDOFF_PTR_H_
