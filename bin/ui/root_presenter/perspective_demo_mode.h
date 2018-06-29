// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_PERSPECTIVE_DEMO_MODE_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_PERSPECTIVE_DEMO_MODE_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/cpp/resources.h"

#include "garnet/bin/ui/root_presenter/displays/display_metrics.h"

namespace root_presenter {

class Presentation;

// This class hooks into Presenter to provide the following behavior: when
// Alt-Equals is pressed, the current display usage is toggled.
class PerspectiveDemoMode {
 public:
  PerspectiveDemoMode();
  // Calls Presentation.DisplayUsage if Alt-Equals is pressed to switch to the
  // next display usage enum value.
  //
  // |event| is the input event.
  // |presenter| is the root presenter.
  // |continue_dispatch_out| is set to false if the event should no longer be
  // dispatched.
  //
  // Returns true if the event was consumed and the scene is to be invalidated.
  bool OnEvent(const fuchsia::ui::input::InputEvent& event,
               Presentation* presenter);

  // Returns true if an animation update happened and the scene is to be
  // invalidated.
  bool UpdateAnimation(Presentation* presenter, uint64_t presentation_time);

  bool WantsClipping() const { return animation_state_ == kDefault; }

 private:
  // Handle the "Perspective Demo" hotkey.  This cycles through the following
  // modes:
  // 1) default UI behavior
  // 2) disable clipping
  // 3) disable clipping + zoomed out perspective view w/ trackball
  // ... and then back to 1).
  //
  // In mode 3), dragging along the bottom 10% of the screen causes the camera
  // to pan/rotate around the stage.
  void HandleAltBackspace(Presentation* presenter);

  enum AnimationState {
    kDefault,
    kCameraMovingAway,
    kCameraReturning,
    kTrackball,
  };
  AnimationState animation_state_ = kDefault;

  // Presentation time at which this presentation last entered either
  // kCameraMovingAway or kCameraReturning state.
  uint64_t animation_start_time_ = 0;

  // State related to managing camera panning in "trackball" mode.
  bool trackball_pointer_down_ = false;
  uint32_t trackball_device_id_ = 0;
  uint32_t trackball_pointer_id_ = 0;
  float trackball_previous_x_ = 0.f;
  float camera_pan_ = 0.f;

  FXL_DISALLOW_COPY_AND_ASSIGN(PerspectiveDemoMode);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_PERSPECTIVE_DEMO_MODE_H_
