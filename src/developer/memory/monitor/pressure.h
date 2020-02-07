// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_H_

#include <fuchsia/memorypressure/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/event.h>
#include <zircon/types.h>

#include <array>
#include <string>
#include <vector>

namespace monitor {

namespace test {
class PressureUnitTest;
class PressureFidlUnitTest;
}  // namespace test

enum Level : size_t {
  kCritical = 0,
  kWarning,
  kNormal,
  kNumLevels,
};
static constexpr size_t kNameLength = 10;
static constexpr std::array<char[kNameLength], Level::kNumLevels + 1> kLevelNames = {
    "CRITICAL", "WARNING", "NORMAL", "UNSET"};

struct WatcherState {
  fuchsia::memorypressure::WatcherPtr proxy;
  Level level_sent;
  bool response_received;
};

class Pressure : public fuchsia::memorypressure::Provider {
 public:
  explicit Pressure(bool watch_for_changes, sys::ComponentContext* context = nullptr,
                    async_dispatcher_t* dispatcher = nullptr);

  // fuchsia::memorypressure::Provider interface
  void RegisterWatcher(fidl::InterfaceHandle<fuchsia::memorypressure::Watcher> watcher) override;

 private:
  zx_status_t InitMemPressureEvents();
  void WatchForChanges();
  void WaitOnLevelChange();
  void OnLevelChanged(zx_handle_t handle);
  void PostLevelChange();
  void ReleaseWatcher(fuchsia::memorypressure::Watcher* watcher);
  void NotifyWatcher(WatcherState& watcher, Level level);
  fuchsia::memorypressure::Level ConvertLevel(Level level);

  std::atomic<Level> level_ = Level::kNumLevels;
  std::array<zx::event, Level::kNumLevels> events_;
  std::array<zx_wait_item_t, Level::kNumLevels> wait_items_;
  async::TaskClosureMethod<Pressure, &Pressure::WatchForChanges> watch_task_{this};
  async::TaskClosureMethod<Pressure, &Pressure::PostLevelChange> post_task_{this};
  async::Loop loop_ = async::Loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  async_dispatcher_t* provider_dispatcher_;
  fidl::BindingSet<fuchsia::memorypressure::Provider> bindings_;
  std::vector<WatcherState> watchers_;

  friend class test::PressureUnitTest;
  friend class test::PressureFidlUnitTest;
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_H_
