// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_INCLUDE_KTL_UNIQUE_PTR_H_
#define ZIRCON_KERNEL_INCLUDE_KTL_UNIQUE_PTR_H_

#include <memory>

#include <fbl/alloc_checker.h>

namespace ktl {

using std::unique_ptr;

template <typename T, typename... Args>
unique_ptr<T> make_unique(fbl::AllocChecker* ac, Args&&... args) {
  return unique_ptr<T>(new (ac) T(std::forward<Args>(args)...));
}

}  // namespace ktl

#endif  // ZIRCON_KERNEL_INCLUDE_KTL_UNIQUE_PTR_H_
