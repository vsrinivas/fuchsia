// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KTL_UNIQUE_PTR_H_
#define ZIRCON_KERNEL_INCLUDE_KTL_UNIQUE_PTR_H_

#include <memory>

#include <fbl/alloc_checker.h>
#include <ktl/type_traits.h>

namespace ktl {

using std::unique_ptr;

template <typename T, typename... Args>
ktl::enable_if_t<!ktl::is_array_v<T>, unique_ptr<T>> make_unique(fbl::AllocChecker* ac,
                                                                 Args&&... args) {
  return unique_ptr<T>(new (ac) T(std::forward<Args>(args)...));
}

// For an unbounded array of given size, each element is default-constructed.
// This is different from plain `new (ac) T[n]`, which leaves it uninitialized.
template <typename T, typename... Args>
ktl::enable_if_t<ktl::is_unbounded_array_v<T>, unique_ptr<T>> make_unique(fbl::AllocChecker* ac,
                                                                          size_t n) {
  return unique_ptr<T>(new (ac) ktl::remove_extent_t<T>[n]());
}

}  // namespace ktl

#endif  // ZIRCON_KERNEL_INCLUDE_KTL_UNIQUE_PTR_H_
