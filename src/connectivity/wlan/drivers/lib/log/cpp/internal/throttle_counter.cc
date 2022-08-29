// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <zircon/syscalls.h>

#include <wlan/drivers/internal/throttle_counter.h>

// Attempt to consume one token. If a token is successfully consumed then one token will be
// deducted and true is returned. Returns false if there are no available tokens.
static bool consume_token(struct throttle_counter* throttle_counter) {
  const zx_ticks_t current_tick = zx_ticks_get();
  const zx_ticks_t ticks_per_token = static_cast<zx_ticks_t>(
      (static_cast<double>(zx_ticks_per_second()) / throttle_counter->tokens_per_second));

  zx_ticks_t old_tick =
      atomic_load_explicit(&throttle_counter->last_issued_tick, std::memory_order_relaxed);

  // If the last tick that we issued a token is further back than the capacity of the bucket we
  // need to update it to be full but not over capacity.
  const zx_ticks_t min_tick = current_tick - (ticks_per_token * throttle_counter->capacity);

  while (true) {
    // This check needs to happen every loop since we might get a new updated_tick if the
    // compare exchange below spuriously fails and returns an old_tick that is too far back.
    // This also happens the first time throttle_counter is used assuming it is initialized
    // with last_issued_tick = 0.
    zx_ticks_t updated_tick = min_tick > old_tick ? min_tick : old_tick;

    // Add the cost of a token to the time we last issued a token, if the total exceeds the
    // current number of ticks that is the same as the cost being too high.
    updated_tick += ticks_per_token;

    if (updated_tick > current_tick) {
      // The number of ticks required to consume a token is too high
      return false;
    }

    if (atomic_compare_exchange_weak_explicit(&throttle_counter->last_issued_tick, &old_tick,
                                              updated_tick, std::memory_order_relaxed,
                                              std::memory_order_relaxed)) {
      // The atomic value didn't change and now contains the updated tick
      return true;
    }
  }
}

bool throttle_counter_consume(struct throttle_counter* throttle_counter, uint64_t* out_counter) {
  if (consume_token(throttle_counter)) {
    // Clear the counter and fetch the previous value atomically, this ensures that in the case
    // of multiple consumes in parallel only one of them will report multiple throttled events.
    *out_counter = atomic_exchange_explicit(&throttle_counter->num_throttled_events, 0,
                                            std::memory_order_relaxed);
    return true;
  }

  // fetch_add returns the old value so add 1
  *out_counter = atomic_fetch_add_explicit(&throttle_counter->num_throttled_events, 1,
                                           std::memory_order_relaxed) +
                 1;
  return false;
}
