// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/display_power_manager.h"

#include <fuchsia/ui/display/internal/cpp/fidl.h>
#include <zircon/errors.h>
#include <zircon/status.h>

#include "src/ui/scenic/lib/display/display_manager.h"

namespace scenic_impl::display {

namespace {

using SetDisplayPowerResult = fuchsia::ui::display::internal::DisplayPower_SetDisplayPower_Result;

}  // namespace

DisplayPowerManager::DisplayPowerManager(DisplayManager* display_manager)
    : display_manager_(display_manager) {
  FX_DCHECK(display_manager_);
}

void DisplayPowerManager::SetDisplayPower(bool power_on, SetDisplayPowerCallback callback) {
  // No display
  if (!display_manager_->default_display()) {
    callback(SetDisplayPowerResult::WithErr(ZX_ERR_NOT_FOUND));
    return;
  }

  // TODO(fxbug.dev/95196): Since currently Scenic only supports one display,
  // the DisplayPowerManager will only control power of the default display.
  // Once Scenic and DisplayManager supports multiple displays, this needs to
  // be updated to control power of all available displays.
  FX_DCHECK(display_manager_->default_display_controller());
  auto id = display_manager_->default_display()->display_id();

  fuchsia::hardware::display::Controller_SetDisplayPower_Result set_display_power_result;
  auto status = (*display_manager_->default_display_controller())
                    ->SetDisplayPower(id, power_on, &set_display_power_result);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to call FIDL SetDisplayPower(): " << zx_status_get_string(status);
    callback(SetDisplayPowerResult::WithErr(ZX_ERR_INTERNAL));
    return;
  }
  if (set_display_power_result.is_err()) {
    FX_LOGS(WARNING) << "DisplayController SetDisplayPower() is not supported; error status: "
                     << zx_status_get_string(set_display_power_result.err());
    callback(SetDisplayPowerResult::WithErr(ZX_ERR_NOT_SUPPORTED));
    return;
  }

  callback(SetDisplayPowerResult::WithResponse({}));
}

}  // namespace scenic_impl::display
