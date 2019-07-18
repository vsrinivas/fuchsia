// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_

#include <fuchsia/recovery/cpp/fidl.h>
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
class FactoryResetManager : public fuchsia::recovery::FactoryResetStateNotifier {
 public:
  FactoryResetManager(component::StartupContext* startup_context);

  // Returns true if the event is handled.
  bool OnMediaButtonReport(const fuchsia::ui::input::MediaButtonsReport& report);

  void SetWatcher(
      fidl::InterfaceHandle<fuchsia::recovery::FactoryResetStateWatcher> watcher) override;

  bool countdown_started() const { return countdown_started_; }

 private:
  class Notifier {
   public:
    explicit Notifier(fuchsia::recovery::FactoryResetStateWatcherPtr watcher);
    void Notify(fuchsia::recovery::FactoryResetState state);

   private:
    void SendIfPending();
    void Send();

    fuchsia::recovery::FactoryResetStateWatcherPtr watcher_;

    // True if a notification has been sent but not acknowledged by the client.
    bool notification_in_progress_ = false;

    // Contains the next state to send to the watcher,
    fuchsia::recovery::FactoryResetState pending_;

    // Contains the last sent deadline.
    fuchsia::recovery::FactoryResetState last_;

    FXL_DISALLOW_COPY_AND_ASSIGN(Notifier);
  };

  void StartFactoryResetCountdown();
  void CancelFactoryResetCountdown();

  void TriggerFactoryReset();

  void NotifyStateChange();

  bool countdown_started_ = false;

  zx_time_t deadline_;

  fuchsia::recovery::FactoryResetPtr factory_reset_;

  fidl::BindingSet<fuchsia::recovery::FactoryResetStateNotifier> notifier_bindings_;

  std::unique_ptr<Notifier> notifier_;

  // We wrap the delayed task we post on the async loop to timeout in a
  // CancelableClosure so we can cancel it if the buttons are released.
  fxl::CancelableClosure reset_after_timeout_;
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_
