// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/zx/event.h>
#include <lib/zx/time.h>

class OneShotEvent {
public:
  OneShotEvent();

  // Signal any current or future callers of Wait().  Cannot be undone.
  void Signal();

  // The just_fail_deadline can be something like
  // zx::deadline_after(zx::msec(5000)) to just fail the whole process after 5
  // seconds if the wait hasn't succeeded by then.  This is test-only code.
  //
  // Passing zx::time::infinite() isn't recommended for tests, since timing out
  // at infra level is more difficult to diagnose.
  void Wait(zx::time just_fail_deadline = zx::time::infinite());
private:
  zx::event event_;
};
