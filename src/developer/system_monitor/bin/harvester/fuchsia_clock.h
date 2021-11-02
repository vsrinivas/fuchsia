// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_FUCHSIA_CLOCK_H_
#define SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_FUCHSIA_CLOCK_H_

#include <lib/async/cpp/wait.h>
#include <lib/zx/clock.h>
#include <lib/zx/time.h>

#include <optional>

#include "src/lib/timekeeper/clock.h"

namespace harvester {

// A wrapper around a clock which handles listening for ZX_CLOCK_STARTED.
//
// Creating this object does not check if the clock has been started. You must
// call |WaitForStart| before any of the time methods on this class will return
// a value.
//
// |dispatcher| is used for waiting on the ZX_CLOCK_STARTED signal.
// |clock| is called to get the current time after the clock has been started.
// |clock_handle| is the handle used for waiting on the started signal.
class FuchsiaClock {
 public:
  explicit FuchsiaClock(async_dispatcher_t* dispatcher,
                        std::unique_ptr<timekeeper::Clock> clock,
                        zx::unowned_clock clock_handle)
      : dispatcher_(dispatcher),
        clock_(std::move(clock)),
        clock_handle_(std::move(clock_handle)),
        started_wait_(this, clock_handle_->get_handle(), ZX_CLOCK_STARTED, 0) {}

  // Returns the current UTC time if the clock has started, otherwise
  // std::nullopt.
  std::optional<timekeeper::time_utc> now();

  // Returns the number of nanoseconds since the epoch if the clock has started,
  // otherwise std::nullopt.
  std::optional<zx_time_t> nanoseconds();

  // Wait for the ZX_CLOCK_STARTED signal.
  //
  // This function will synchronously check if the signal has been set and if so
  // will call mark the clock as being started and call |callback| before this
  // returns.
  //
  // If that check fails, this will asynchronously wait for the signal and will
  // call |callback| whenever the signal occurs.
  //
  // If waiting on the signal is canceled by receiving a response with status
  // ZX_ERR_CANCELED due to the clock handle being closed, the callback will
  // be called with that status. In all other situations, the callback should be
  // called with ZX_OK to indicate the clock has started.
  void WaitForStart(std::function<void(zx_status_t)> callback);

 private:
  void OnClockStarted(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                      zx_status_t status, const zx_packet_signal_t* signal);

  std::atomic_bool started_ = false;
  async_dispatcher_t* dispatcher_;
  std::unique_ptr<timekeeper::Clock> clock_;
  // This handle must be listed here before started_wait_ below to ensure proper
  // initialization order.
  zx::unowned_clock clock_handle_;
  async::WaitMethod<FuchsiaClock, &FuchsiaClock::OnClockStarted> started_wait_;
  std::optional<std::function<void(zx_status_t)>> started_callback_;
};

}  // namespace harvester

#endif  // SRC_DEVELOPER_SYSTEM_MONITOR_BIN_HARVESTER_FUCHSIA_CLOCK_H_
