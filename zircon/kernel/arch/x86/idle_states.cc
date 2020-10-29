// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT
#include <arch/x86/idle_states.h>

#include <assert.h>
#include <bits.h>
#include <ktl/atomic.h>
#include <platform.h>
#include <zircon/time.h>

#include <arch/x86/feature.h>

namespace {

constexpr int kIdleDurationFactor = 3;

constexpr uint32_t StateNumberFromMwaitHint(uint32_t hint) { return BITS_SHIFT(hint, 8, 4) + 1; }

}  // namespace

const x86_idle_states_t* x86_get_idle_states() { return &x86_get_microarch_config()->idle_states; }

int x86_num_idle_states(const x86_idle_states_t* states) {
  int num;
  for (num = 0; num < X86_MAX_CSTATES; ++num) {
    if (x86_is_base_idle_state(states->states + num)) {
      return num + 1;
    }
  }
  // The config must include and end with X86_CSTATE_C1.
  return -1;
}

X86IdleStates::X86IdleStates(const x86_idle_states_t* states) : last_idle_duration_(0U) {
  auto num_states = x86_num_idle_states(states);
  ASSERT_MSG(num_states > 0, "Invalid C-state configuration: Expected at least C1 to be defined.");
  num_states_ = static_cast<size_t>(num_states);
  for (unsigned i = 0; i < num_states_; ++i) {
    states_[i] = X86IdleState(states->states + i);
  }
  state_mask_ = states->default_state_mask | 0x1;  // Always allow C1
}

X86IdleState* X86IdleStates::PickIdleState() {
  if (last_idle_duration_ == 0) {
    // Return the shallowest state (C1).
    return &states_[num_states_ - 1];
  }
  const uint32_t valid_state_mask = state_mask_.load(ktl::memory_order_relaxed);
  // Pick the deepest valid state which has ExitLatency less than
  // kIdleDurationFactor * <expected idle duration>
  for (unsigned i = 0; i < num_states_; ++i) {
    auto& state = states_[i];
    auto state_num = StateNumberFromMwaitHint(state.MwaitHint());
    if (!(BIT_SET(valid_state_mask, state_num - 1))) {
      continue;
    }
    if (state.ExitLatency() < kIdleDurationFactor * last_idle_duration_) {
      return &state;
    }
  }
  return &states_[num_states_ - 1];
}
