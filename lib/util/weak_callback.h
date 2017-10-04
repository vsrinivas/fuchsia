// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_UTIL_WEAK_CALLBACK_H_
#define PERIDOT_LIB_UTIL_WEAK_CALLBACK_H_

#include <functional>
#include "lib/fxl/memory/weak_ptr.h"

namespace modular {

// This wraps the given |unsafe_callback| with an additional check that
// |weak_ptr| exists when this callback is called.
// TODO(vardhan): Remove this in favour of scoped_callback.h (currently it's
// internally scoped under ledger).
template <typename T>
std::function<void()> WeakCallback(
    fxl::WeakPtr<T> weak_ptr,
    const std::function<void()>& unsafe_callback) {
  return [ weak_ptr = std::move(weak_ptr), unsafe_callback ] {
    if (weak_ptr) {
      unsafe_callback();
    }
  };
}

}  // namespace modular

#endif  // PERIDOT_LIB_UTIL_WEAK_CALLBACK_H_
