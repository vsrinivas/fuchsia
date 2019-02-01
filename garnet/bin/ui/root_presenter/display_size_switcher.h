// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_SIZE_SWITCHER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_SIZE_SWITCHER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/cpp/resources.h"

#include "garnet/bin/ui/root_presenter/displays/display_metrics.h"

namespace root_presenter {

class Presentation;

// This class hooks into Presenter to provide the following behavior: when
// Alt-Hyphen is pressed, the display size is toggled to the next value of
// several preset sizes.
class DisplaySizeSwitcher {
 public:
  DisplaySizeSwitcher() = default;

  // Calls Presentation.SetDisplaySizeInPixels if Alt-Hyphen is pressed to
  // switch to the next preset size.
  //
  // |event| is the input event.
  // |presenter| is the root presenter.
  //
  // Returns true if the event was consumed and the scene is to be invalidated.
  bool OnEvent(const fuchsia::ui::input::InputEvent& event,
               Presentation* presenter);

 private:
  uint32_t current_display_size_index_ = 0;
  FXL_DISALLOW_COPY_AND_ASSIGN(DisplaySizeSwitcher);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_SIZE_SWITCHER_H_
