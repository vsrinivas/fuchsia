// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_

#include <fuchsia/recovery/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>

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
  FactoryResetManager(component::StartupContext* startup_context);

  // Returns true if the event is handled.
  bool OnMediaButtonReport(
      const fuchsia::ui::input::MediaButtonsReport& report);

  bool countdown_started() const { return countdown_started_; }

 private:
  void StartFactoryResetCountdown();
  void CancelFactoryResetCountdown();

  void TriggerFactoryReset();

  bool countdown_started_ = false;

  fuchsia::recovery::FactoryResetPtr factory_reset_;

  // We wrap the delayed task we post on the async loop to timeout in a
  // CancelableClosure so we can cancel it if the buttons are released.
  fxl::CancelableClosure reset_after_timeout_;
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_FACTORY_RESET_MANAGER_H_
