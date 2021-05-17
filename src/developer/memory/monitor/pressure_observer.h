// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_OBSERVER_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_OBSERVER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/zx/event.h>
#include <zircon/types.h>

#include <array>
#include <memory>
#include <string>

namespace monitor {

namespace test {
class PressureObserverUnitTest;
class PressureNotifierUnitTest;
}  // namespace test

enum Level : size_t {
  kImminentOOM = 0,
  kCritical,
  kWarning,
  kNormal,
  kNumLevels,
};
static constexpr size_t kNameLength = 15;
static constexpr std::array<char[kNameLength], Level::kNumLevels + 1> kLevelNames = {
    "IMMINENT-OOM", "CRITICAL", "WARNING", "NORMAL", "UNSET"};

class PressureNotifier;

class PressureObserver {
 public:
  explicit PressureObserver(bool watch_for_changes, PressureNotifier* notifier = nullptr);
  PressureObserver(const PressureObserver&) = delete;
  PressureObserver& operator=(const PressureObserver&) = delete;
  ~PressureObserver();

  Level GetCurrentLevel() const { return level_; }
  Level GetCurrentLevelForWatcher() const;

 private:
  zx_status_t InitMemPressureEvents();
  void WatchForChanges();
  void WaitOnLevelChange();
  void OnLevelChanged(zx_handle_t handle);

  // Start off with Normal level before the right kernel level has been discovered, so that
  // PressureNotifier notifies clients with a valid level until the level has been initialized.
  //
  // We can end up in this unitialized state if a watcher registers before the PressureObserver has
  // discovered the initial system memory pressure level. Since watcher registration is supposed to
  // return the current level, advertize the current level as Normal. This is fine bacause when we
  // do initialize the level, we will send another signal if it was not Normal.
  //
  // In practice this will typically happen in tests which create a separate monitor instance and
  // do not have access to the root job to be able to query and initialize the memory pressure
  // level.
  std::atomic<Level> level_ = Level::kNormal;
  std::array<zx::event, Level::kNumLevels> events_;
  std::array<zx_wait_item_t, Level::kNumLevels> wait_items_;
  async::TaskClosureMethod<PressureObserver, &PressureObserver::WatchForChanges> watch_task_{this};
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  PressureNotifier* const notifier_;
  bool level_initialized_ = false;

  friend class test::PressureObserverUnitTest;
  friend class test::PressureNotifierUnitTest;
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_OBSERVER_H_
