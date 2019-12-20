// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_WATCHDOG_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_WATCHDOG_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/clock.h>
#include <lib/zx/timer.h>

#include <mutex>

#include "video_decoder.h"

class Watchdog {
 public:
  class Owner {
   public:
    // This may get spurious wakeups, so CheckAndResetTimeout should be called after grabbing all
    // the relevant locks.
    virtual void OnSignaledWatchdog() = 0;
  };

  explicit Watchdog(Owner* owner);

  void Start();
  void Cancel();

  // Returns true if the watchdog is timed out, and also resets the watchdog if that happened.
  bool CheckAndResetTimeout();

  bool is_running();

 private:
  void HandleTimer(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                   const zx_packet_signal_t* signal);

  Owner* owner_;
  std::mutex mutex_;
  __TA_GUARDED(mutex_)
  zx::time timeout_time_;
  __TA_GUARDED(mutex_)
  bool timer_running_ = false;

  zx::timer timer_;
  async::WaitMethod<Watchdog, &Watchdog::HandleTimer> waiter_{this};
  async::Loop loop_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_WATCHDOG_H_
