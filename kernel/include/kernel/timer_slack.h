// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

enum slack_mode : uint32_t {
    TIMER_SLACK_CENTER, // slack is centered around deadline
    TIMER_SLACK_LATE,   // slack interval is [deadline, deadline + slack)
    TIMER_SLACK_EARLY,  // slack interval is (deadline - slack, deadline]
};

// TimerSlack specifies how much a timer or event is allowed to deviate from its deadline.
class TimerSlack {
public:
    // Create a TimerSlack object with the specified |amount| and |mode|.
    //
    // |amount| must be >= 0. 0 means "no slack" (i.e. no coalescing is allowed).
    constexpr TimerSlack(zx_duration_t amount, slack_mode mode)
        : amount_(amount), mode_(mode) {
        DEBUG_ASSERT(amount_ >= 0);
    }

    zx_duration_t amount() const { return amount_; }

    slack_mode mode() const { return mode_; }

    bool operator==(const TimerSlack& rhs) const {
        return amount_ == rhs.amount_ &&
               mode_ == rhs.mode_;
    }

    bool operator!=(const TimerSlack& rhs) const {
        return !operator==(rhs);
    }

private:
    zx_duration_t amount_;
    slack_mode mode_;
};

// TimerSlack is passed by value so make sure it'll fit in two 64-bit registers.
static_assert(sizeof(TimerSlack) <= 16);

// Used to indicate that a given deadline is not eligible for coalescing.
//
// Not intended to be used for timers/events that originate on behalf of usermode.
static constexpr TimerSlack kNoSlack = TimerSlack(0, TIMER_SLACK_CENTER);
