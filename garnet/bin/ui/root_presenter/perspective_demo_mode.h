// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_PERSPECTIVE_DEMO_MODE_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_PERSPECTIVE_DEMO_MODE_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include "src/lib/fxl/macros.h"
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

  bool WantsClipping() const { return animation_state_ == kOrthographic; }

 private:
  // Handle the "Perspective Demo" hotkey.  This cycles through the following
  // modes:
  // 1) Orthographic view
  // 2) Disable clipping + zoomed out perspective view w/ trackball control
  // ... and then back to 1).
  // 3) Disable clipping + perspective view
  //
  // In mode 2), dragging along the bottom 10% of the screen causes the camera
  // to pan/rotate around the stage.
  // In mode 3), dragging along the bottom 10% of the screen causes the camera
  // to change fov.
  void HandleAltBackspace(Presentation* presenter);

  // Maps from a normalized zoom value [0.f 1.f] to a value from a range of
  // valid half-fovs (avoiding divide by zero and near/far clip region
  // issues).
  float ComputeHalfFov(Presentation* presenter, float camera_zoom) const;

  // If pan_param = 0.f, and zoom_param = 0.f, this function will produce a
  // perspective camera very close to the orthographic camera. Otherwise,
  // pan_param will scrub through an animation to a three-quarters view, and
  // zoom_param will interpolate from a nearly-orthographic camera, to a
  // configurable perspective camera.
  void UpdateCamera(Presentation* presentation, float pan_param,
                    float zoom_param);

  enum AnimationState {
    kOrthographic,
    kAnimateToThreeQuarters,
    kThreeQuarters,
    kAnimateToPerspective,
    kPerspective,
    kAnimateToOrthographic,
  };
  AnimationState animation_state_ = kOrthographic;

  // Presentation time at which this presentation last entered either the
  // kAnimateToThreeQuarters, kAnimateToPerspective, or kAnimateToOrthographic
  // states.
  uint64_t animation_start_time_ = 0;
  float target_camera_pan_ = 0.0f;
  float target_camera_zoom_ = 0.0f;

  // State related to managing camera panning in "trackball" mode.
  bool trackball_pointer_down_ = false;
  uint32_t trackball_device_id_ = 0;
  uint32_t trackball_pointer_id_ = 0;
  float trackball_previous_x_ = 0.f;

  FXL_DISALLOW_COPY_AND_ASSIGN(PerspectiveDemoMode);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_PERSPECTIVE_DEMO_MODE_H_
