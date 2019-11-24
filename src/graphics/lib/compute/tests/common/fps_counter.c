// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fps_counter.h"

#include <stddef.h>
#include <stdio.h>
#include <sys/time.h>

// Return current time in milliseconds.
static double
time_now_seconds(void * opaque)
{
  struct timeval tm;
  gettimeofday(&tm, NULL);
  return tm.tv_sec * 1.0 + (tm.tv_usec / 1e9);
}

// Set this to 0 to disable unit-testing support.
#define SUPPORT_UNIT_TESTING 1

#if SUPPORT_UNIT_TESTING

static fps_counter_clock_callback_t s_clock_callback = &time_now_seconds;
static void *                       s_clock_opaque   = NULL;

// Ensure that the implementation uses |clock_callback(clock_opaque)| to get
// the current time during unit-testing.
void
fps_counter_set_clock_for_testing(fps_counter_clock_callback_t clock_callback, void * clock_opaque)
{
  if (!clock_callback)
    {
      clock_callback = &time_now_seconds;
      clock_opaque   = NULL;
    }
  s_clock_callback = clock_callback;
  s_clock_opaque   = clock_opaque;
}

#define GET_CLOCK_SECONDS() s_clock_callback(s_clock_opaque)
#else  // !SUPPORT_UNIT_TESTING
#define GET_CLOCK_SECONDS() time_now_seconds(NULL)
#endif  // !SUPPORT_UNIT_TESTING

#define SECONDS_INCREMENT 4.0

void
fps_counter_start(fps_counter_t * fps)
{
  fps->current_fps      = 0.;
  fps->start_time       = GET_CLOCK_SECONDS();
  fps->next_time        = fps->start_time + SECONDS_INCREMENT;
  fps->frame_count      = 0;
  fps->frame_count_prev = 0;
}

bool
fps_counter_tick(fps_counter_t * fps)
{
  fps->frame_count++;

  double now_secs = GET_CLOCK_SECONDS();
  if (now_secs < fps->next_time)
    return false;

  fps->current_fps      = (fps->frame_count - fps->frame_count_prev) / (now_secs - fps->start_time);
  fps->frame_count_prev = fps->frame_count;
  fps->start_time       = fps->next_time;
  while (fps->next_time <= now_secs)
    fps->next_time += SECONDS_INCREMENT;

  return true;
}

bool
fps_counter_stop(fps_counter_t * fps)
{
  if (fps->frame_count > fps->frame_count_prev)
    return fps_counter_tick(fps);

  return false;
}

void
fps_counter_tick_and_print(fps_counter_t * fps)
{
  if (fps_counter_tick(fps))
    {
      printf("FPS: %1.f\n", fps->current_fps);
      fflush(stdout);
    }
}

void
fps_counter_stop_and_print(fps_counter_t * fps)
{
  if (fps->frame_count > fps->frame_count_prev)
    fps_counter_tick_and_print(fps);
}
