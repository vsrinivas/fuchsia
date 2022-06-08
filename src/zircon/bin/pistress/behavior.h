// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_ZIRCON_BIN_PISTRESS_BEHAVIOR_H_
#define SRC_ZIRCON_BIN_PISTRESS_BEHAVIOR_H_

#include <stdint.h>
#include <zircon/syscalls/profile.h>
#include <zircon/time.h>

#include <random>

enum class ProfileType { Fair, Deadline };

struct LingerBehavior {
  const float spin_probability = 0.5f;
  const float linger_probability = 0.0f;
  std::uniform_int_distribution<zx_duration_t> time_dist{0, 0};
};

struct TestThreadBehavior {
  // Default profile for the thread is a Fair profile with default weight.
  const ProfileType profile_type = ProfileType::Fair;
  const uint32_t priority = ZX_PRIORITY_DEFAULT;
  const uint64_t period = 0;
  const uint64_t deadline = 0;
  const uint64_t capacity = 0;

  // By default, threads do not linger at the intermediate stages of SyncObj
  // acquisition.
  LingerBehavior intermediate_linger{.linger_probability = 0.0f, .time_dist{0, 0}};

  // By default, threads always linger for somewhere between [0.1, 20] mSec in
  // the final stage of SyncObj acquisition.
  LingerBehavior final_linger{.linger_probability = 1.0f, .time_dist{ZX_USEC(100), ZX_MSEC(20)}};

  // By default, threads will obtain somewhere between 1 and 6 sync objects
  // during a cycle.
  std::uniform_int_distribution<size_t> path_len_dist{1, 6};

  // By default, threads will have a 20% chance of using a timeout of somewhere
  // between [0.05, 5] mSec during a sync object acquisition operation.
  const float timeout_prob = 0.20f;
  std::uniform_int_distribution<zx_duration_t> timeout_dist{ZX_USEC(50), ZX_MSEC(5)};

  // By default, threads have a low probability (0.5%) of changing their own
  // profile at any stage of of a cycle.
  const float self_profile_change_prob = 0.005f;
};

#endif  // SRC_ZIRCON_BIN_PISTRESS_BEHAVIOR_H_
