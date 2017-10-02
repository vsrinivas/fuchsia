// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CALLBACK_ENSURE_COPYABLE_H_
#define PERIDOT_BIN_LEDGER_CALLBACK_ENSURE_COPYABLE_H_

#include <type_traits>

#include "lib/fxl/functional/make_copyable.h"

namespace callback {
namespace internal {

template <typename C, typename = void>
struct EnsureCopyable {};

template <typename C>
struct EnsureCopyable<
    C,
    typename std::enable_if<std::is_copy_constructible<C>::value>::type> {
  static C Apply(C&& c) { return std::forward<C>(c); }
};

template <typename C>
struct EnsureCopyable<
    C,
    typename std::enable_if<!std::is_copy_constructible<C>::value>::type> {
  static auto Apply(C&& c) { return fxl::MakeCopyable(std::forward<C>(c)); }
};

}  // namespace internal

// Returns a copyable function object that will forward its call to |lambda|.
//
// If // |lambda| itself is copyable, this function ensures that the resulting
// object has the same type as |lambda|.
template <typename T>
auto EnsureCopyable(T&& lambda) {
  return ::callback::internal::EnsureCopyable<T>::Apply(
      std::forward<T>(lambda));
}

}  // namespace callback

#endif  // PERIDOT_BIN_LEDGER_CALLBACK_ENSURE_COPYABLE_H_
