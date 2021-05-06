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
  using NotifyCb = fit::function<void(Level)>;

  explicit PressureNotifier(bool watch_for_changes, bool send_critical_pressure_crash_reports,
                            sys::ComponentContext* context = nullptr,
                            async_dispatcher_t* dispatcher = nullptr, NotifyCb notify_cb = nullptr);
  
  PressureNotifier(const PressureNotifier&) = delete;
  PressureNotifier& operator=(const PressureNotifier&) = delete;

  // fuchsia::memorypressure::Provider interface
  void RegisterWatcher(fidl::InterfaceHandle<fuchsia::memorypressure::Watcher> watcher) override;

  // Notify watchers of a pressure level change.
  void Notify();

  // Notify watchers with a simulated memory pressure |level|. For diagnostic use by MemoryDebugger.
  void DebugNotify(fuchsia::memorypressure::Level level) const;

 private:
  void PostLevelChange();
  void ReleaseWatcher(fuchsia::memorypressure::Watcher* watcher);
  void OnLevelChangedCallback(WatcherState* watcher);
  void NotifyWatcher(WatcherState* watcher, Level level);

  bool CanGenerateNewCriticalCrashReports();
  enum CrashReportType : uint8_t {
    kImminentOOM,
    kCritical,
  };
  void FileCrashReport(CrashReportType type);

  async::TaskClosureMethod<PressureNotifier, &PressureNotifier::PostLevelChange> post_task_{this};
  async_dispatcher_t* const provider_dispatcher_;
  sys::ComponentContext* const context_;
  NotifyCb notify_cb_;
  fidl::BindingSet<fuchsia::memorypressure::Provider> bindings_;
  std::vector<std::unique_ptr<WatcherState>> watchers_;
  PressureObserver observer_;

  bool observed_normal_level_ = true;
  zx::time prev_critical_crash_report_time_ = zx::time(ZX_TIME_INFINITE_PAST);
  zx::duration critical_crash_report_interval_ = zx::min(30);
  const bool send_critical_pressure_crash_reports_;

  friend class test::PressureNotifierUnitTest;
};

}  // namespace monitor

#endif  // SRC_DEVELOPER_MEMORY_MONITOR_PRESSURE_NOTIFIER_H_
