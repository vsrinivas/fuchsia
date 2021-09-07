// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_CXXABI_DYNAMIC_INIT_INCLUDE_LIB_CXXABI_DYNAMIC_INIT_CXXABI_DYNAMIC_INIT_H_
#define ZIRCON_KERNEL_LIB_CXXABI_DYNAMIC_INIT_INCLUDE_LIB_CXXABI_DYNAMIC_INIT_CXXABI_DYNAMIC_INIT_H_

#include <stdint.h>
#include <zircon/assert.h>

// This header provides Acquire, Release, and Abort operations for static initializer guard
// variables.  The functions __cxa_guard_acquire, __cxa_guard_release, and __cxa_guard_abort are
// defined in the .cc file and are simply trivial wrappers around Acquire, Release, and Abort in
// order to facilitate testing.
//
// See also //zircon/kernel/lib/cxxabi-dynamic-init/README.md.

namespace cxxabi_dynamic_init {

namespace internal {

// Returns true iff global constructors have completed.
//
// This function must be implemented by the kernel or by tests.
bool ConstructorsCalled();

// Itanium C++ ABI for initialization guard variables says:
//
// "The size of the guard variable is 64 bits. The first byte (i.e. the byte at the address of the
// full variable) shall contain the value 0 prior to initialization of the associated variable, and
// 1 after initialization is complete. Usage of the other bytes of the guard variable is
// implementation-defined."
//
// See https://itanium-cxx-abi.github.io/cxx-abi/abi.html#guards.

// ABI dictates that the first byte (byte at the address of the guard object) is either 0x00 or 0x1
// to indicate whether the object has been initialized (0x01) or not (0x00).  The rest of guard
// object is free to be used as we see fit.  We'll use the LSB of the second byte to indicate
// whether the guard object is in use (i.e. held).
constexpr uint64_t kFirstByteMask = 0x00000000000000ff;
constexpr uint64_t kInUseMask = 0x0000000000000100;

// Returns true iff the object guarded by |guard_object| is already initialized.
inline bool IsObjectInitialized(const uint64_t* guard_object) {
  return (*guard_object & kFirstByteMask) != 0;
}

// Marks the object guarded by |guard_object| as initialized.
inline void SetObjectInitialized(uint64_t* guard_object) {
  *guard_object = (*guard_object & ~kFirstByteMask) | 1;
}

// Returns true iff |guard_object| is in use.
inline bool IsInUse(const uint64_t* guard_object) { return (*guard_object & kInUseMask) != 0; }

// Indicate that |guard_object| is in use (i.e. initialization is in progress).
inline void SetInUse(uint64_t* guard_object) { *guard_object |= kInUseMask; }

// Indicate that |guard_object| is no longer in use (i.e. initialization has been aborted).
inline void SetNotInUse(uint64_t* guard_object) { *guard_object &= ~kInUseMask; }

}  // namespace internal

inline int Acquire(uint64_t* guard_object) {
  ZX_DEBUG_ASSERT(!internal::ConstructorsCalled());
  if (internal::IsObjectInitialized(guard_object)) {
    return 0;
  }

  ZX_DEBUG_ASSERT(!internal::IsInUse(guard_object));
  internal::SetInUse(guard_object);
  return 1;
}

inline void Release(uint64_t* guard_object) {
  ZX_DEBUG_ASSERT(!internal::ConstructorsCalled());
  internal::SetObjectInitialized(guard_object);
}

inline void Abort(uint64_t* guard_object) {
  ZX_DEBUG_ASSERT(!internal::ConstructorsCalled());
  internal::SetNotInUse(guard_object);
}

}  // namespace cxxabi_dynamic_init

#endif  // ZIRCON_KERNEL_LIB_CXXABI_DYNAMIC_INIT_INCLUDE_LIB_CXXABI_DYNAMIC_INIT_CXXABI_DYNAMIC_INIT_H_
