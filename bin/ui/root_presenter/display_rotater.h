// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_ROTATER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_ROTATER_H_

#include "lib/fxl/macros.h"
#include "lib/ui/input/fidl/input_events.fidl.h"
#include "lib/ui/scenic/client/resources.h"

namespace root_presenter {

class Presentation;

// This class plugs in "Display flip" behavior to the Presenter; i.e. the
// display gets flipped when a particular key (volume down) is pressed.
class DisplayRotater {
 public:
  DisplayRotater();
  // Modifies |scene| if a volume down key press is detected by rotating it 180
  // degrees.
  //
  // |Presentation| is the root presenter.
  //
  // Returns true if the scene should be invalidated.
  bool OnEvent(const input::InputEventPtr& event, Presentation* presentation);

 private:
  void FlipDisplay(Presentation* presentation);

  bool display_flipped_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayRotater);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_ROTATER_H_
