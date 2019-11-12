// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_WATCHDOG_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_WATCHDOG_H_

#include <lib/async/cpp/time.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <thread>

namespace display {

class Watchdog {
 public:
  void Init(async_dispatcher_t* dispatcher, zx::duration delay, const char* message) {
    dispatcher_ = dispatcher;
    delay_ = delay;
    message_ = message;
  }

  // Run a watchdog thread which will crash if Reset is not called.
  int Run();

  // Stop will cause Run() to exit without crashing.
  void Stop();

  // Resets the watchdog timer. Must be called more frequently than |delay_| or it will fire.
  void Reset();

 private:
  void Crash();

  std::atomic<bool> running_ = true;
  std::atomic<zx::time> reset_time_;
  async_dispatcher_t* dispatcher_ = nullptr;
  zx::duration delay_;
  const char* message_;
};

}  // namespace display

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_DISPLAY_WATCHDOG_H_
