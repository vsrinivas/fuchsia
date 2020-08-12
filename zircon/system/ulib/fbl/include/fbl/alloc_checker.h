// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_ALLOC_CHECKER_H_
#define FBL_ALLOC_CHECKER_H_

#include <memory>
#include <new>
#include <utility>
#include <stddef.h>

namespace fbl {

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
  AllocChecker();
  ~AllocChecker();
  void arm(size_t size, bool result);
  bool check();

 private:
  unsigned state_;
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

}  // namespace internal

template <typename T, typename... Args>
typename internal::unique_type<T>::single make_unique_checked(AllocChecker* ac, Args&&... args) {
  return std::unique_ptr<T>(new (ac) T(std::forward<Args>(args)...));
}

}  // namespace fbl

void* operator new(size_t size, std::align_val_t align, fbl::AllocChecker* ac) noexcept;
void* operator new[](size_t size, std::align_val_t align, fbl::AllocChecker* ac) noexcept;

void* operator new(size_t size, fbl::AllocChecker* ac) noexcept;
void* operator new[](size_t size, fbl::AllocChecker* ac) noexcept;

#endif  // FBL_ALLOC_CHECKER_H_
