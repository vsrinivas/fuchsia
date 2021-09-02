// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.power.button/cpp/wire.h>
#include <zircon/compiler.h>

#ifndef SRC_BRINGUP_BIN_PWRBTN_MONITOR_MONITOR_H_
#define SRC_BRINGUP_BIN_PWRBTN_MONITOR_MONITOR_H_

namespace pwrbtn {

class PowerButtonMonitor : public fidl::WireServer<fuchsia_power_button::Monitor> {
  using Action = fuchsia_power_button::wire::Action;

 public:
  void GetAction(GetActionRequestView view, GetActionCompleter::Sync& completer) override;
  void SetAction(SetActionRequestView view, SetActionCompleter::Sync& completer) override;

  // Called when the power button is pressed.
  zx_status_t DoAction();

 private:
  static zx_status_t SendPoweroff();

  Action action_ = Action::kShutdown;
};

}  // namespace pwrbtn

#endif  // SRC_BRINGUP_BIN_PWRBTN_MONITOR_MONITOR_H_
