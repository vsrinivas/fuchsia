// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_PAGER_WATCHDOG_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_PAGER_WATCHDOG_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace blobfs {
namespace pager {

class PagerWatchdog {
 public:
  explicit PagerWatchdog(zx::duration duration);
  PagerWatchdog(PagerWatchdog&& o) = delete;
  PagerWatchdog& operator=(PagerWatchdog&& o) = delete;
  PagerWatchdog(const PagerWatchdog&) = delete;
  PagerWatchdog& operator=(const PagerWatchdog&) = delete;
  ~PagerWatchdog();

  // RAII wrapper which manages an ongoing timer.
  // When the token goes out of scope, the timer is cancelled.
  class ArmToken {
   public:
    ArmToken(PagerWatchdog& watchdog, zx::duration duration);
    ArmToken(const ArmToken& o) = delete;
    ArmToken& operator=(const ArmToken& o) = delete;
    ArmToken(ArmToken&& o) = delete;
    ArmToken& operator=(ArmToken&& o) = delete;
    ~ArmToken();

    zx::ticks deadline() const { return deadline_; }

   private:
    PagerWatchdog& watchdog_;
    zx::ticks deadline_;
  };

  // Arms the watchdog to fire after its configured duration.
  // Each |ArmToken| represents a contract that the watchdog is armed to fire when the configured
  // duration has passed since the |ArmToken| was created.
  // The watchdog is disarmed when no |ArmToken| exists.
  ArmToken Arm() { return ArmWithDuration(duration_); }
  ArmToken ArmWithDuration(zx::duration duration);

  // Exposed for testing. If set, *only* the callback is invoked; no logging is performed.
  void SetCallback(std::function<void(int)> callback) { callback_ = std::move(callback); }

  // Exposed for testing. If any tasks are scheduled, blocks until the task fires.
  void RunUntilIdle();

 private:
  friend class ArmToken;

  void OnDeadlineMissed(int count);
  void Thread();

  const zx::duration duration_;
  std::optional<std::function<void(int)>> callback_;
  std::mutex mutex_;
  std::condition_variable_any condition_;
  ArmToken* token_ TA_GUARDED(mutex_) = nullptr;
  bool terminate_ TA_GUARDED(mutex_) = false;
  std::thread thread_;
};

}  // namespace pager
}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_PAGER_WATCHDOG_H_
