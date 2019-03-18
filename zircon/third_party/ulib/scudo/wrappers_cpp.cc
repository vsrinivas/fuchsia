//===-- wrappers_cpp.cc -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "platform.h"

// Skip this compilation unit if compiled as part of Bionic.
#if !SCUDO_ANDROID || !_BIONIC

#include "allocator.h"

#include <stdint.h>

extern scudo::Allocator<scudo::Config> *AllocatorPtr;

namespace std {
struct nothrow_t {};
enum class align_val_t : size_t {};
} // namespace std

INTERFACE void *operator new(size_t size) {
  return AllocatorPtr->allocate(size, scudo::FromNew);
}
INTERFACE void *operator new[](size_t size) {
  return AllocatorPtr->allocate(size, scudo::FromNewArray);
}
INTERFACE void *operator new(size_t size, std::nothrow_t const &) NOEXCEPT {
  return AllocatorPtr->allocate(size, scudo::FromNew);
}
INTERFACE void *operator new[](size_t size, std::nothrow_t const &) NOEXCEPT {
  return AllocatorPtr->allocate(size, scudo::FromNewArray);
}
INTERFACE void *operator new(size_t size, std::align_val_t align) {
  return AllocatorPtr->allocate(size, scudo::FromNew, (scudo::uptr)align);
}
INTERFACE void *operator new[](size_t size, std::align_val_t align) {
  return AllocatorPtr->allocate(size, scudo::FromNewArray, (scudo::uptr)align);
}
INTERFACE void *operator new(size_t size, std::align_val_t align,
                             std::nothrow_t const &) NOEXCEPT {
  return AllocatorPtr->allocate(size, scudo::FromNew, (scudo::uptr)align);
}
INTERFACE void *operator new[](size_t size, std::align_val_t align,
                               std::nothrow_t const &) NOEXCEPT {
  return AllocatorPtr->allocate(size, scudo::FromNewArray, (scudo::uptr)align);
}

INTERFACE void operator delete(void *ptr)NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNew);
}
INTERFACE void operator delete[](void *ptr) NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNewArray);
}
INTERFACE void operator delete(void *ptr, std::nothrow_t const &)NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNew);
}
INTERFACE void operator delete[](void *ptr, std::nothrow_t const &) NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNewArray);
}
INTERFACE void operator delete(void *ptr, size_t size)NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNew, size);
}
INTERFACE void operator delete[](void *ptr, size_t size) NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNewArray, size);
}
INTERFACE void operator delete(void *ptr, std::align_val_t align)NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNew, 0, (scudo::uptr)align);
}
INTERFACE void operator delete[](void *ptr, std::align_val_t align) NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNewArray, 0, (scudo::uptr)align);
}
INTERFACE void operator delete(void *ptr, std::align_val_t align,
                               std::nothrow_t const &)NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNew, 0, (scudo::uptr)align);
}
INTERFACE void operator delete[](void *ptr, std::align_val_t align,
                                 std::nothrow_t const &) NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNewArray, 0, (scudo::uptr)align);
}
INTERFACE void operator delete(void *ptr, size_t size,
                               std::align_val_t align)NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNew, size, (scudo::uptr)align);
}
INTERFACE void operator delete[](void *ptr, size_t size,
                                 std::align_val_t align) NOEXCEPT {
  AllocatorPtr->deallocate(ptr, scudo::FromNewArray, size, (scudo::uptr)align);
}

#endif // !SCUDO_ANDROID || !_BIONIC
