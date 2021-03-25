// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_ALLOC_CHECKER_H_
#define SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_ALLOC_CHECKER_H_

#include <stddef.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <memory>
#include <new>
#include <utility>

namespace operation {

// An object which is passed to operator new to allow client code to handle
// allocation failures.  Once armed by operator new, the client must call `check()`
// to verify the state of the allocation checker before it goes out of scope.
//
// Use it like this:
//
//     AllocChecker ac;
//     MyObject* obj = new (&ac) MyObject();
//     if (!ac.check()) {
//         // handle allocation failure (obj will be null)
//     }
class AllocChecker {
 public:
  AllocChecker() = default;
  ~AllocChecker() {
    if (ZX_DEBUG_ASSERT_IMPLEMENTED && unlikely(armed_)) {
      CheckNotCalledPanic();
    }
  }

  // Arm the AllocChecker. Once armed, `check` must be called prior to destruction.
  void arm(size_t size, bool result) {
    if (ZX_DEBUG_ASSERT_IMPLEMENTED && unlikely(armed_)) {
      ArmedTwicePanic();
    }
    armed_ = true;
    ok_ = (size == 0 || result);
  }

  // Return true if the previous allocation succeeded.
  bool check() {
    armed_ = false;
    return likely(ok_);
  }

 private:
  // If called, abort program execution with an error.
  [[noreturn]] static void CheckNotCalledPanic();
  [[noreturn]] static void ArmedTwicePanic();

  bool armed_ = false;
  bool ok_ = false;
};

namespace internal {

template <typename T>
struct unique_type {
  using single = std::unique_ptr<T>;
};

template <typename T>
struct unique_type<T[]> {
  using incomplete_array = std::unique_ptr<T[]>;
};

inline void* checked(size_t size, AllocChecker* ac, void* mem) {
  ac->arm(size, mem != nullptr);
  return mem;
}

}  // namespace internal

}  // namespace operation

// Versions of the C++ `new` operator that accept an AllocChecker.
//
// These can be used as follows:
//
//    operation::AllocChecker ac;
//    Object* foo = new (&ac) Object();
//    if (!ac.check()) {
//      // failed.
//    }
//    // ...
//
//   * This uses the standard C++ library std::nothrow_t versions of the `new` operator.
//     These work better with sanitizers such as ASAN that ensure that the correct `new`/`new[]`
//     operator is paired with the correct `delete`/`delete[]` operator.
//
inline void* operator new(size_t size, operation::AllocChecker* ac) noexcept {
  return operation::internal::checked(size, ac, operator new(size, std::nothrow_t()));
}
inline void* operator new(size_t size, std::align_val_t align,
                          operation::AllocChecker* ac) noexcept {
  return operation::internal::checked(size, ac, operator new(size, align, std::nothrow_t()));
}
inline void* operator new[](size_t size, operation::AllocChecker* ac) noexcept {
  return operation::internal::checked(size, ac, operator new[](size, std::nothrow_t()));
}
inline void* operator new[](size_t size, std::align_val_t align,
                            operation::AllocChecker* ac) noexcept {
  return operation::internal::checked(size, ac, operator new[](size, align, std::nothrow_t()));
}

#endif  // SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_ALLOC_CHECKER_H_
