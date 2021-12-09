// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>

#include <lib/concurrent/seqlock.inc>

namespace concurrent {
namespace internal {

struct FuchsiaUserModeOsal {
  static inline zx_time_t GetClockMonotonic() { return zx_clock_get_monotonic(); }
  static inline void ArchYield() {}
};

// Manually expand the SeqLock template using the Fuchsia user-mode OSAL
template class SeqLock<FuchsiaUserModeOsal>;

}  // namespace internal
}  // namespace concurrent
