// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_MEMORY_PRESSURE_HANDLER_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_MEMORY_PRESSURE_HANDLER_H_

#include <fidl/fuchsia.memorypressure/cpp/fidl.h>
#include <fidl/fuchsia.virtualization/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/client.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>

class MemoryPressureHandler
    : public fidl::AsyncEventHandler<fuchsia_memorypressure::Watcher>,
      public fidl::AsyncEventHandler<fuchsia_memorypressure::Provider>,
      public fidl::AsyncEventHandler<fuchsia_virtualization::BalloonController>,
      public fidl::Server<fuchsia_memorypressure::Watcher> {
 public:
  constexpr static zx::duration kBalloonInflateCompletionWaitTime = zx::sec(1);
  constexpr static zx::duration kBalloonRepeatedInflateWaitTime = zx::min(1);
  constexpr static uint64_t kBalloonAvailableMemoryInflatePercentage = 90;

  explicit MemoryPressureHandler(async_dispatcher_t* dispatcher);

  zx_status_t Start(sys::ComponentContext* context);

 private:
  enum class TargetBalloonState {
    Inflated,
    Deflated,
  };
  async_dispatcher_t* dispatcher_;
  fidl::Client<fuchsia_virtualization::BalloonController> balloon_controller_;
  std::optional<fidl::ServerBindingRef<fuchsia_memorypressure::Watcher>> memory_pressure_server_;
  bool delayed_task_scheduled_ = false;
  zx::time last_inflate_time_;
  TargetBalloonState target_balloon_state_ = TargetBalloonState::Deflated;

  void on_fidl_error(fidl::UnbindInfo error) override;
  // |fuchsia::memorypressure::Watcher|
  void OnLevelChanged(OnLevelChangedRequest& request,
                      OnLevelChangedCompleter::Sync& completer) override;

  void UpdateTargetBalloonSize();
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_MEMORY_PRESSURE_HANDLER_H_
