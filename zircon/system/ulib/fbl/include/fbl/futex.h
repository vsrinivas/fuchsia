// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_FUTEX_H_
#define FBL_FUTEX_H_

#include <zircon/types.h>

#include "fbl/atomic_ref.h"

namespace fbl {

// A small helper class which wraps a zx_futex_t, provides atomic manipulation,
// and guarantees that we can properly get the address of the underlying storage
// (which we cannot technically do with std::atomic)
//
// TODO(johngro) - come back and clean this up once fxbug.dev/27097 has been resolved.

class futex_t {
 public:
  constexpr explicit futex_t(zx_futex_t value) : value_(value) {}

  futex_t(const futex_t&) = delete;
  futex_t(futex_t&&) = delete;
  futex_t& operator=(const futex_t&) = delete;
  futex_t& operator=(futex_t&&) = delete;

  zx_futex_t load(memory_order order = memory_order_seq_cst) {
    atomic_ref<zx_futex_t> ref(value_);
    return ref.load(order);
  }

  void store(zx_futex_t value, memory_order order = memory_order_seq_cst) {
    atomic_ref<zx_futex_t> ref(value_);
    ref.store(value, order);
  }

  zx_futex_t* operator&() { return &value_; }
  const zx_futex_t* operator&() const { return &value_; }

 private:
  zx_futex_t value_;
};

}  // namespace fbl

#endif  // FBL_FUTEX_H_
