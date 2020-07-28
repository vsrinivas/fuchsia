// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_PAGER_WATCHDOG_H_
#define ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_PAGER_WATCHDOG_H_

#ifndef __Fuchsia__
#error Fuchsia-only Header
#endif

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/status.h>

#include <memory>
#include <optional>

namespace blobfs {
namespace pager {

class PagerWatchdog {
 public:
  PagerWatchdog() = default;
  PagerWatchdog(PagerWatchdog&& o) = delete;
  PagerWatchdog& operator=(PagerWatchdog&& o) = delete;
  PagerWatchdog(const PagerWatchdog&) = delete;
  PagerWatchdog& operator=(const PagerWatchdog&) = delete;

  // Creates an instance of |PagerWatchdog| with a timeout duration of |duration|.
  static zx::status<std::unique_ptr<PagerWatchdog>> Create(zx::duration duration);

  // RAII wrapper which manages an ongoing timer.
  // When the token goes out of scope, the timer is cancelled.
  class ArmToken {
   public:
    ArmToken(PagerWatchdog* owner, async_dispatcher_t* dispatcher, zx::duration duration);
    ArmToken(const ArmToken& o) = delete;
    ArmToken(ArmToken&& o) = delete;
    ArmToken& operator=(const ArmToken& o) = delete;
    ArmToken& operator=(ArmToken&& o) = delete;

    friend class PagerWatchdog;

   private:
    void OnDeadlineMissed();

    PagerWatchdog* owner_ = nullptr;
    async::TaskClosureMethod<ArmToken, &ArmToken::OnDeadlineMissed> deadline_missed_task_{this};
  };

  // Arms the watchdog to fire after its configured duration.
  // Each |ArmToken| represents a contract that the watchdog is armed to fire when the configured
  // duration has passed since the |ArmToken| was created.
  // The watchdog is disarmed when no |ArmToken| exists.
  ArmToken Arm();

  // Exposed for testing. If set, *only* the callback is invoked; no logging is performed.
  void SetCallback(std::function<void()> callback) { callback_ = std::move(callback); }

  // Exposed for testing. If any tasks are scheduled, blocks until the task fires.
  void RunUntilIdle() { loop_.RunUntilIdle(); }

 private:
  explicit PagerWatchdog(zx::duration duration);
  void OnDeadlineMissed();

  zx::duration duration_;
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  std::optional<std::function<void()>> callback_;
};

}  // namespace pager
}  // namespace blobfs

#endif  // ZIRCON_SYSTEM_ULIB_BLOBFS_PAGER_PAGER_WATCHDOG_H_
