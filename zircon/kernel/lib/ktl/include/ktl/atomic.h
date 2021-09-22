// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ATOMIC_H_
#define ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ATOMIC_H_

#include <lib/stdcompat/atomic.h>

#include <atomic>

namespace ktl {

using std::atomic;

using std::memory_order;

using std::memory_order_acq_rel;
using std::memory_order_acquire;
using std::memory_order_consume;
using std::memory_order_relaxed;
using std::memory_order_release;
using std::memory_order_seq_cst;

using std::atomic_init;

using std::atomic_signal_fence;
using std::atomic_thread_fence;

using cpp20::atomic_ref;

}  // namespace ktl

#endif  // ZIRCON_KERNEL_LIB_KTL_INCLUDE_KTL_ATOMIC_H_
