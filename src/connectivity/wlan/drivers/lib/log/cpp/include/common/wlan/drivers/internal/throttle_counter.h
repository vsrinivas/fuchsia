// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_INTERNAL_THROTTLE_COUNTER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_INTERNAL_THROTTLE_COUNTER_H_

#if defined(__cplusplus)
extern "C++" {
#include <atomic>
}
// NOLINTNEXTLINE(bugprone-reserved-identifier)
#define _Atomic(T) std::atomic<T>
#else  // defined(__cplusplus)
#include <stdatomic.h>
#endif

#include <stdint.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Struct that fixes the rate at which some event occurs to an upper bound.
// Typical usage of this class looks as follows:
//
// static struct throttle_counter tc = {
//    .capacity = CAPACITY, .tokens_per_second = RATE, .num_throttled_events = 0, .last_issued_tick
//    = 0
// };
// uint64_t num_throttled = 0;
// if (throttle_counter_consume(&tc, num_throttled)) {
//  DO_SOMETHING();
// }
//
// Conceptually, throttle_counter works by generating tokens at a fixed rate. Users can then
// "consume" a token to call some rate-limited function. Both the generation and consumption of
// tokens is handled by the call to throttle_counter_consume().
//
// This class is thread-safe if used with throttle_counter_consume().
struct throttle_counter {
  // The maximum number of tokens that can be stored by this counter, if throttle_counter_consume()
  // is not called for a long time.
  zx_ticks_t capacity;

  // The rate at which tokens are generated. This is a double, so throttle_counter can generate
  // tokens at a rate lower than once per second. Proper usage of this class requires a positive
  // tokens_per_second value. Using a negative tokens_per_second is undefined.
  double tokens_per_second;

  // Counts the number of times the user attempts to consume a token without succeeding.
  // Should be initialized to 0.
  //
  // After initialization, this field should only be used internally by throttle_counter_consume().
  _Atomic(uint64_t) num_throttled_events;

  // Stores the last time a token was issued.
  // Should be initialized to INT64_MIN to ensure that tokens can be issued immediately on startup.
  // If this is initialized to 0, then the caller may have to wait some time for the first token
  // to be generated.
  //
  // After initialization, this field should only be used internally by throttle_counter_consume().
  _Atomic(zx_ticks_t) last_issued_tick;
};

// Attempt to consume a token to use for logging.
//
// If the consume is successful then this function will return True and the previous number of
// throttled events is placed in |out_counter|. Note that if this function failed to consume a token
// previously, on the first successful call to throttle_counter_consume(), |out_counter| will
// contain the number of previously throttled events (not zero). On a successful consume,
// throttle_counter.num_throttled_events is reset to 0.
//
// If the consume fails then this function will return False and the number of throttled events,
// INCLUDING this one, is placed in |out_counter|.
//
// NOTE: Consuming the token and setting out_counter is NOT a single atomic operation. If two
// threads call consume on a throttle_counter with 1 token and N previously throttled events,
// only one of these threads will succeed in their call to consume but both may get a value of
// N + 1 in |out_counter|. This happens when the thread that successfully consumes the token is
// interrupted before it can set |out_counter|, and the thread that failed to consume the token
// runs and increments num_throttled_events. We allow this edge case to occur to avoid the overhead
// of using locks in throttle_counter.
bool throttle_counter_consume(struct throttle_counter* throttle_counter, uint64_t* out_counter);

__END_CDECLS

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_LOG_CPP_INCLUDE_COMMON_WLAN_DRIVERS_INTERNAL_THROTTLE_COUNTER_H_
