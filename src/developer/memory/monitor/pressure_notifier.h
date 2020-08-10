// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_NOTIFIER_H_
#define SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_NOTIFIER_H_

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/memorypressure/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <vector>

#include "src/developer/memory/monitor/pressure_observer.h"

namespace monitor {

struct WatcherState {
  fuchsia::memorypressure::WatcherPtr proxy;
  Level level_sent;
  bool pending_callback;
  bool needs_free;
};

class PressureNotifier : public fuchsia::memorypressure::Provider {
 public:
  explicit PressureNotifier(bool watch_for_changes, sys::ComponentContext* context = nullptr,
                            async_dispatcher_t* dispatcher = nullptr);
  PressureNotifier(const PressureNotifier&) = delete;
  PressureNotifier& operator=(const PressureNotifier&) = delete;

  // fuchsia::memorypressure::Provider interface
  void RegisterWatcher(fidl::InterfaceHandle<fuchsia::memorypressure::Watcher> watcher) override;

  void Notify();

 private:
  void PostLevelChange();
  void ReleaseWatcher(fuchsia::memorypressure::Watcher* watcher);
  void OnLevelChangedCallback(WatcherState* watcher);
  void NotifyWatcher(WatcherState* watcher, Level level);
  fuchsia::memorypressure::Level ConvertLevel(Level level) const;

  bool CanGenerateNewCrashReports();
  void FileCrashReport();

  async::TaskClosureMethod<PressureNotifier, &PressureNotifier::PostLevelChange> post_task_{this};
  async_dispatcher_t* const provider_dispatcher_;
  sys::ComponentContext* const context_;
  fidl::BindingSet<fuchsia::memorypressure::Provider> bindings_;
  std::vector<std::unique_ptr<WatcherState>> watchers_;
  PressureObserver observer_;

  bool observed_normal_level_ = true;
  zx::time prev_crash_report_time_ = zx::time(ZX_TIME_INFINITE_PAST);
  zx::duration crash_report_interval_ = zx::min(30);

  friend class test::PressureNotifierUnitTest;
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_NOTIFIER_H_
