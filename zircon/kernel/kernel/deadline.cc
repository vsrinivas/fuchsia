// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "kernel/deadline.h"

#include <zircon/time.h>

const TimerSlack TimerSlack::none_{0, TIMER_SLACK_CENTER};

const Deadline Deadline::infinite_{ZX_TIME_INFINITE, TimerSlack::none()};

zx_time_t Deadline::earliest() const {
  switch (slack_.mode()) {
    case TIMER_SLACK_CENTER:
      return zx_time_sub_duration(when_, slack_.amount());
    case TIMER_SLACK_LATE:
      return when_;
    case TIMER_SLACK_EARLY:
      return zx_time_sub_duration(when_, slack_.amount());
    default:
      panic("invalid timer mode %u\n", slack_.mode());
  }
}

zx_time_t Deadline::latest() const {
  switch (slack_.mode()) {
    case TIMER_SLACK_CENTER:
      return zx_time_add_duration(when_, slack_.amount());
    case TIMER_SLACK_LATE:
      return zx_time_add_duration(when_, slack_.amount());
    case TIMER_SLACK_EARLY:
      return when_;
    default:
      panic("invalid timer mode %u\n", slack_.mode());
  }
}
