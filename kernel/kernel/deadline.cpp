// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <kernel/deadline.h>

const TimerSlack TimerSlack::none_{0, TIMER_SLACK_CENTER};

const Deadline Deadline::infinite_{ZX_TIME_INFINITE, TimerSlack::none()};
