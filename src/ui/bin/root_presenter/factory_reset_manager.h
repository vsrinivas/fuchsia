// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_
#define SRC_UI_BIN_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_

#include <fuchsia/media/sounds/cpp/fidl.h>
#include <fuchsia/recovery/cpp/fidl.h>
#include <fuchsia/recovery/policy/cpp/fidl.h>
#include <fuchsia/recovery/ui/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/ui/bin/root_presenter/media_retriever.h"

namespace root_presenter {

constexpr zx::duration kResetCountdownDuration = zx::sec(10);
constexpr zx::duration kButtonCountdownDuration = zx::msec(500);

enum class FactoryResetState {
  ALLOWED,           // Factory reset is allowed by policy.
  DISALLOWED,        // Factory reset is disallowed by policy.
  BUTTON_COUNTDOWN,  // Countdown before factory reset starts counting down.
  RESET_COUNTDOWN,   // Countdown until factory reset is triggered.
  TRIGGER_RESET      // Factory reset is being triggered.
};

// This class hooks into Presenter to provide the following behavior:
// when the FDR button or both volume buttons are pressed, count down to
// 10 seconds. If the buttons aren't released before the countdown is over,
// trigger factory reset.
class FactoryResetManager : public fuchsia::recovery::policy::Device {
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

  FactoryResetManager(sys::ComponentContext& component_context,
                      std::shared_ptr<MediaRetriever> media_retriever);

  // Returns true if the event is handled.
  bool OnMediaButtonReport(const fuchsia::ui::input::MediaButtonsReport& report);

  FactoryResetState factory_reset_state() const { return factory_reset_state_; }

 private:
  // Handles MediaButtonReports and returns true if the report is processed.
  bool HandleReportOnAllowedState(const fuchsia::ui::input::MediaButtonsReport& report);
  bool HandleReportOnDisallowedState(const fuchsia::ui::input::MediaButtonsReport& report);
  bool HandleReportOnButtonCountdown(const fuchsia::ui::input::MediaButtonsReport& report);
  bool HandleReportOnResetCountdown(const fuchsia::ui::input::MediaButtonsReport& report);

  void StartFactoryResetCountdown();

  void PlayCompleteSoundThenReset();
  void TriggerFactoryReset();

  void NotifyStateChange();

  // Changes policy to enable or disable factory reset.
  void SetIsLocalResetAllowed(bool allowed) override;

  fuchsia::recovery::ui::FactoryResetCountdownState State() const;

  FactoryResetState factory_reset_state_ = FactoryResetState::ALLOWED;

  // The time when a factory reset is scheduled to happen. Only valid if coundown_started_ is true
  zx_time_t deadline_ = 0u;

  fuchsia::recovery::FactoryResetPtr factory_reset_;
  fuchsia::media::sounds::PlayerPtr sound_player_;
  std::shared_ptr<MediaRetriever> media_retriever_;

  fidl::BindingSet<fuchsia::recovery::ui::FactoryResetCountdown, std::unique_ptr<WatchHandler>>
      countdown_bindings_;
  fidl::BindingSet<fuchsia::recovery::policy::Device> policy_bindings_;

  // We wrap the delayed task we post on the async loop to timeout in a
  // CancelableClosure so we can cancel it if the buttons are released.
  fxl::CancelableClosure start_reset_countdown_after_timeout_;
  fxl::CancelableClosure reset_after_timeout_;
};

}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_
