// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_POWER_MANAGER_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_POWER_MANAGER_H_

#include <fuchsia/ui/display/internal/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>

#include <optional>

#include "src/lib/fxl/macros.h"
#include "src/ui/scenic/lib/display/display.h"
#include "src/ui/scenic/lib/display/display_controller_listener.h"
#include "src/ui/scenic/lib/display/display_manager.h"

namespace scenic_impl::display {

// Implements the |fuchsia::ui::display::internal::DisplayPower| protocol,
// Internal protocol clients are able to control the power of all available
// display devices through this protocol.
class DisplayPowerManager : public fuchsia::ui::display::internal::DisplayPower {
 public:
  explicit DisplayPowerManager(DisplayManager* display_manager);

  // |fuchsia::ui::display::internal::DisplayPower|
  void SetDisplayPower(bool power_on, SetDisplayPowerCallback callback) override;

  fidl::InterfaceRequestHandler<DisplayPower> GetHandler() { return bindings_.GetHandler(this); }

 private:
  DisplayManager* display_manager_ = nullptr;
  fidl::BindingSet<fuchsia::ui::display::internal::DisplayPower> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayPowerManager);
};

}  // namespace scenic_impl::display

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_DISPLAY_POWER_MANAGER_H_
