// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_USAGE_SWITCHER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_USAGE_SWITCHER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/cpp/resources.h"

#include "garnet/bin/ui/root_presenter/displays/display_metrics.h"

namespace root_presenter {

class Presentation;

std::string GetDisplayUsageAsString(fuchsia::ui::policy::DisplayUsage usage);

// This class hooks into Presenter to provide the following behavior: when
// Alt-Equals is pressed, the current display usage is toggled.
class DisplayUsageSwitcher {
 public:
  DisplayUsageSwitcher();
  // Calls Presentation.DisplayUsage if Alt-Equals is pressed to switch to the
  // next display usage enum value.
  //
  // |event| is the input event.
  // |presenter| is the root presenter.
  //
  // Returns true if the scene should be invalidated.
  bool OnEvent(const fuchsia::ui::input::InputEvent& event,
               Presentation* presenter);

 private:
  uint32_t current_display_usage_index_ = 0;
  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayUsageSwitcher);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_USAGE_SWITCHER_H_
