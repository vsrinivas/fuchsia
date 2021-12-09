// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <time.h>

#include <lib/concurrent/seqlock.inc>

namespace concurrent {
namespace internal {

struct PosixUserModeOsal {
  static inline zx_time_t GetClockMonotonic() {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
      return (static_cast<zx_time_t>(ts.tv_sec) * 1'000'000'000) +
             static_cast<zx_time_t>(ts.tv_nsec);
    } else {
      assert(false);
      return 0;
    }
  }

  static inline void ArchYield() {}
};

// Manually expand the SeqLock template using the Fuchsia user-mode OSAL
template class SeqLock<PosixUserModeOsal>;

}  // namespace internal
}  // namespace concurrent
