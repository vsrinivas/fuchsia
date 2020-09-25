// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_FUTEX_H_
#define FBL_FUTEX_H_

#include <zircon/types.h>

namespace fbl {

// A small helper class which wraps a zx_futex_t, provides atomic manipulation,
// and guarantees that we can properly get the address of the underlying storage
// (which we cannot technically do with std::atomic)
//
// TODO(johngro) - come back and clean this up once fxbug.dev/27097 has been resolved.

enum memory_order : int {
  memory_order_relaxed = __ATOMIC_RELAXED,
  memory_order_acquire = __ATOMIC_ACQUIRE,
  memory_order_release = __ATOMIC_RELEASE,
  memory_order_acq_rel = __ATOMIC_ACQ_REL,
  memory_order_seq_cst = __ATOMIC_SEQ_CST,
};

class futex_t {
 public:
  constexpr futex_t(zx_futex_t value) : value_(value) {}

  futex_t(const futex_t&) = delete;
  futex_t(futex_t&&) = delete;
  futex_t& operator=(const futex_t&) = delete;
  futex_t& operator=(futex_t&&) = delete;

  zx_futex_t load(memory_order order = memory_order_seq_cst) const {
    return __atomic_load_n(&value_, order);
  }

  void store(zx_futex_t value, memory_order order = memory_order_seq_cst) {
    return __atomic_store_n(&value_, value, order);
  }

  zx_futex_t* operator&() { return &value_; }
  const zx_futex_t* operator&() const { return &value_; }

 private:
  zx_futex_t value_;
};

}  // namespace fbl

#endif  // FBL_FUTEX_H_
