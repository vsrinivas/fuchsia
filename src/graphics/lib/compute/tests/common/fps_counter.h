// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_FPS_COUNTER_H_
#define SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_FPS_COUNTER_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Small helper struct to implement a basic frame/s counter.
//
// Usage is:
//   1) Call fps_counter_start() to start the counter.
//   2) On every frame, call fps_counter_tick_and_print()
//   3) Call fps_counter_stop_and_print() to stop the counter.
//
// The _and_print() suffix means the functions will print the FPS count to
// stdout directly every 4 seconds.
//
// If you don't want to print anything do the following:
//
//   1) Call fps_counter_start() to start the counter.
//
//   2) Call fps_counter_tick() after each rendered frame, and
//      fps_counter_stop() to stop the counter. Both function return true
//      when the current frame/s value was updated. In this case, it can be
//      read from |counter->current_fps|.
//
typedef struct
{
  // Current frame/seconds value. Only valid after fps_counter_tick()
  // or fps_counter_stop() return true.
  double current_fps;

  // private
  double start_time;
  double next_time;
  double frame_count;
  double frame_count_prev;
} fps_counter_t;

// Start the counter.
extern void
fps_counter_start(fps_counter_t * fps);

// Mark the end of one rendered frame. If this returns true, then the value
// of |fps->current_fps| was updated and can be read by the client.
extern bool
fps_counter_tick(fps_counter_t * fps);

// Stop the counter. If this returns true, then the value of |fps->current_fps|
// was updated and can be read by the client.
extern bool
fps_counter_stop(fps_counter_t * fps);

// Convenience function that calls fps_counter_tick() then prints the FPS
// count to stdout if it returns true.
extern void
fps_counter_tick_and_print(fps_counter_t * fps);

// Convenience function that calls fps_counter_stop() then prints the FPS
// count to stdout if it returns true.
extern void
fps_counter_stop_and_print(fps_counter_t * fps);

// Ensure that the implementation uses |clock_callback(clock_opaque)| to get
// the current time during unit-testing.
typedef double (*fps_counter_clock_callback_t)(void * opaque);

extern void
fps_counter_set_clock_for_testing(fps_counter_clock_callback_t clock_callback, void * clock_opaque);

#ifdef __cplusplus
}
#endif

#endif  // SRC_GRAPHICS_LIB_COMPUTE_TESTS_COMMON_FPS_COUNTER_H_
