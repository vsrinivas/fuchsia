// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_PISTRESS_GLOBAL_STATS_H_
#define SRC_ZIRCON_BIN_PISTRESS_GLOBAL_STATS_H_

#include <stdint.h>

#include <atomic>

struct GlobalStats {
  std::atomic<uint64_t> mutex_acquires{0};
  std::atomic<uint64_t> mutex_acq_timeouts{0};
  std::atomic<uint64_t> mutex_releases{0};
  std::atomic<uint64_t> condvar_acquires{0};
  std::atomic<uint64_t> condvar_releases{0};
  std::atomic<uint64_t> condvar_waits{0};
  std::atomic<uint64_t> condvar_acq_timeouts{0};
  std::atomic<uint64_t> condvar_signals{0};
  std::atomic<uint64_t> condvar_bcasts{0};
  std::atomic<uint64_t> intermediate_spins{0};
  std::atomic<uint64_t> intermediate_sleeps{0};
  std::atomic<uint64_t> final_spins{0};
  std::atomic<uint64_t> final_sleeps{0};
  std::atomic<uint64_t> profiles_changed{0};
  std::atomic<uint64_t> profiles_reverted{0};
};

inline GlobalStats global_stats;

#endif  // SRC_ZIRCON_BIN_PISTRESS_GLOBAL_STATS_H_
