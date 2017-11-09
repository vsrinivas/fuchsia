// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_FLIPPER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_FLIPPER_H_

#include "lib/fxl/macros.h"
#include "lib/ui/input/fidl/input_events.fidl.h"
#include "lib/ui/scenic/client/resources.h"

namespace root_presenter {

// This class plugs in "Display flip" behavior to the Presenter; i.e. the
// display gets flipped when a particular key (volume down) is pressed.
class DisplayFlipper {
 public:
  DisplayFlipper();
  // Modifies |scene| if a volume down key press is detected by rotating it 180
  // degrees.
  //
  // If display is flipped and |event| is a touch, modifies |event|'s x and y to
  // account for display rotation.
  //
  // |display_info| is the current DisplayInfo.
  // |scene| is the root Scene node.
  // |continue_dispatch_out| is set to false if the event should no longer be
  // dispatched.
  //
  // Returns true if the scene should be invalidated.
  bool OnEvent(const mozart::InputEventPtr& event,
               scenic_lib::Scene* scene,
               const scenic::DisplayInfoPtr& display_info,
               bool* continue_dispatch_out);

 private:
  void FlipDisplay(scenic_lib::Scene* scene,
                   const scenic::DisplayInfoPtr& display_info);
  static std::pair<float, float> FlipPointerCoordinates(
      float x,
      float y,
      const scenic::DisplayInfoPtr& display_info);

  bool display_flipped_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayFlipper);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_FLIPPER_H_
