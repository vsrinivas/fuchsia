// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_UTILS_UTC_CLOCK_READY_WATCHER_H_
#define SRC_DEVELOPER_FORENSICS_UTILS_UTC_CLOCK_READY_WATCHER_H_

#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/zx/clock.h>

namespace forensics {

class UtcClockReadyWatcherBase {
 public:
  virtual ~UtcClockReadyWatcherBase() = default;

  // Register a callback that will be executed when the utc clock becomes ready.
  virtual void OnClockReady(::fit::callback<void()> callback) = 0;
  virtual bool IsUtcClockReady() const = 0;
};

// Waits for signal from the system indicating the UTC clock is ready, then notifies interested
// parties.
class UtcClockReadyWatcher : public UtcClockReadyWatcherBase {
 public:
  UtcClockReadyWatcher(async_dispatcher_t* dispatcher, zx::unowned_clock clock_handle);
  ~UtcClockReadyWatcher() override = default;

  // Register a callback that will be executed when the utc clock becomes ready.
  void OnClockReady(::fit::callback<void()> callback) override;
  bool IsUtcClockReady() const override;

 private:
  void OnClockStart(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                    const zx_packet_signal_t* signal);

  std::vector<::fit::callback<void()>> callbacks_;
  bool is_utc_clock_ready_ = false;

  async::WaitMethod<UtcClockReadyWatcher, &UtcClockReadyWatcher::OnClockStart>
      wait_for_clock_start_{this};
};
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_UTILS_UTC_CLOCK_READY_WATCHER_H_
