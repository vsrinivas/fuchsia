// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_DEADLINE_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_DEADLINE_H_

#include <assert.h>
#include <platform.h>
#include <zircon/types.h>

enum slack_mode : uint32_t {
  TIMER_SLACK_CENTER = ZX_TIMER_SLACK_CENTER,  // slack is centered around deadline
  TIMER_SLACK_EARLY = ZX_TIMER_SLACK_EARLY,    // slack interval is (deadline - slack, deadline]
  TIMER_SLACK_LATE = ZX_TIMER_SLACK_LATE,      // slack interval is [deadline, deadline + slack)
};

// TimerSlack specifies how much a timer or event is allowed to deviate from its deadline.
class TimerSlack {
 public:
  // Create a TimerSlack object with the specified |amount| and |mode|.
  //
  // |amount| must be >= 0. 0 means "no slack" (i.e. no coalescing is allowed).
  constexpr TimerSlack(zx_duration_t amount, slack_mode mode) : amount_(amount), mode_(mode) {
    DEBUG_ASSERT(amount_ >= 0);
  }

  // Used to indicate that a given deadline is not eligible for coalescing.
  //
  // Not intended to be used for timers/events that originate on behalf of usermode.
  static constexpr const TimerSlack& none() { return none_; }

  constexpr zx_duration_t amount() const { return amount_; }

  constexpr slack_mode mode() const { return mode_; }

  bool operator==(const TimerSlack& rhs) const {
    return amount_ == rhs.amount_ && mode_ == rhs.mode_;
  }

  bool operator!=(const TimerSlack& rhs) const { return !operator==(rhs); }

 private:
  static const TimerSlack none_;

  zx_duration_t amount_;
  slack_mode mode_;
};

// Deadline specifies when a timer or event should occur.
//
// This class encapsulates the point in time at which a timer/event should occur ("when") and how
// much the timer/event is allowed to deviate from that point in time ("slack").
class Deadline {
 public:
  constexpr Deadline(zx_time_t when, TimerSlack slack) : when_(when), slack_(slack) {}

  static constexpr Deadline no_slack(zx_time_t when) { return Deadline(when, TimerSlack::none()); }

  // Construct a deadline using relative duration measured from now.
  static Deadline after(zx_duration_t after, TimerSlack slack = TimerSlack::none()) {
    return Deadline(zx_time_add_duration(current_time(), after), slack);
  }

  // A deadline that will never be reached.
  static constexpr const Deadline& infinite() { return infinite_; }

  constexpr zx_time_t when() const { return when_; }

  constexpr TimerSlack slack() const { return slack_; }

  // Returns the earliest point in time at which this deadline may occur.
  zx_time_t earliest() const;

  // Returns the latest point in time at which this deadline may occur.
  zx_time_t latest() const;

 private:
  static const Deadline infinite_;

  const zx_time_t when_;
  const TimerSlack slack_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_DEADLINE_H_
