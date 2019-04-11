// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_UNIQUE_PTR_H_
#define FBL_UNIQUE_PTR_H_

#include <stdlib.h>
#include <fbl/alloc_checker.h>
#include <fbl/macros.h>
#include <fbl/recycler.h>
#include <zircon/compiler.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace fbl {

template <typename T>
using unique_ptr = std::unique_ptr<T>;

// Comparison against nullptr operators (of the form, nullptr == myptr) for T and T[]
template <typename T>
static inline bool operator==(decltype(nullptr), const unique_ptr<T>& ptr) {
    return (ptr.get() == nullptr);
}

template <typename T>
static inline bool operator!=(decltype(nullptr), const unique_ptr<T>& ptr) {
    return (ptr.get() != nullptr);
}

namespace internal {

template <typename T>
struct unique_type {
    using single = unique_ptr<T>;
};

template <typename T>
struct unique_type<T[]> {
    using incomplete_array = unique_ptr<T[]>;
};

} // namespace internal

template <typename T, typename... Args>
typename internal::unique_type<T>::single
make_unique_checked(AllocChecker* ac, Args&&... args) {
    return unique_ptr<T>(new (ac) T(std::forward<Args>(args)...));
}

}  // namespace fbl

#endif  // FBL_UNIQUE_PTR_H_
