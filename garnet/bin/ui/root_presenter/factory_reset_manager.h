// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_

#include <fuchsia/recovery/cpp/fidl.h>
#include <fuchsia/recovery/ui/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/lib/fxl/functional/cancelable_callback.h"

namespace component {
class StartupContext;
}

namespace root_presenter {

constexpr zx::duration kCountdownDuration = zx::sec(10);

// This class hooks into Presenter to provide the following behavior:
// when the FDR button or both volume buttons are pressed, count down to
// 10 seconds. If the buttons aren't released before the countdown is over,
// trigger factory reset.
class FactoryResetManager {
 public:
  // Handler per FIDL connection to FactoryResetCountdown which keeps track of any hanging
  // callbacks and calls them back on change.
  class WatchHandler : public fuchsia::recovery::ui::FactoryResetCountdown {
   public:
    explicit WatchHandler(const fuchsia::recovery::ui::FactoryResetCountdownState& initialState);

    // Called whenever the factory reset state is changed by the manager
    void OnStateChange(const fuchsia::recovery::ui::FactoryResetCountdownState& state);

    // |fuchsia::recovery::ui::FactoryResetCountdown|
    void Watch(WatchCallback callback) override;

   private:
    void SendIfChanged();

    fuchsia::recovery::ui::FactoryResetCountdownState current_state_;
    bool last_state_sent_ = false;
    // Contains the hanging get if present
    WatchCallback hanging_get_;

    FXL_DISALLOW_COPY_AND_ASSIGN(WatchHandler);
  };

  explicit FactoryResetManager(component::StartupContext* startup_context);

  // Returns true if the event is handled.
  bool OnMediaButtonReport(const fuchsia::ui::input::MediaButtonsReport& report);

  bool countdown_started() const { return countdown_started_; }

 private:
  void StartFactoryResetCountdown();
  void CancelFactoryResetCountdown();

  void TriggerFactoryReset();

  void NotifyStateChange();
 
  fuchsia::recovery::ui::FactoryResetCountdownState State() const;

  bool countdown_started_ = false;

  // The time when a factory reset is scheduled to happen. Only valid if coundown_started_ is true
  zx_time_t deadline_ = 0u;

  fuchsia::recovery::FactoryResetPtr factory_reset_;

  fidl::BindingSet<fuchsia::recovery::ui::FactoryResetCountdown, std::unique_ptr<WatchHandler>>
      countdown_bindings_;

  // We wrap the delayed task we post on the async loop to timeout in a
  // CancelableClosure so we can cancel it if the buttons are released.
  fxl::CancelableClosure reset_after_timeout_;
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_
