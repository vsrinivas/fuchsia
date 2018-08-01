// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_ROTATER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_ROTATER_H_

#if defined(countof)
// Workaround for compiler error due to Zircon defining countof() as a macro.
// Redefines countof() using GLM_COUNTOF(), which currently provides a more
// sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif
#include <glm/ext.hpp>

#include <fuchsia/ui/input/cpp/fidl.h>

#include "garnet/lib/ui/scenic/util/rk4_spring_simulation.h"
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/cpp/resources.h"

namespace root_presenter {

class Presentation;

// This class plugs in "Display flip" behavior to the Presenter; i.e. the
// display gets flipped when a particular key (volume down) is pressed.
class DisplayRotater {
 public:
  DisplayRotater();
  // Modifies |scene| if a volume down key press is detected by rotating it
  // 180 degrees.
  //
  // |Presentation| is the root presenter.
  //
  // Returns true if the scene should be invalidated.
  bool OnEvent(const fuchsia::ui::input::InputEvent& event,
               Presentation* presentation);

  void SetDisplayRotation(Presentation* p, float display_rotation_degrees,
                          bool animate);

  // Returns true if an animation update happened and the scene is to be
  // invalidated.
  bool UpdateAnimation(Presentation* presenter, uint64_t presentation_time);

  // Returns the raw pointer coordinates transformed by the current display
  // rotation.
  glm::vec2 RotatePointerCoordinates(Presentation* p, float x, float y);

 private:
  void FlipDisplay(Presentation* presentation);

  // Presentation time at which animation values were last set.
  uint64_t last_animation_update_time_ = 0;

  scenic::RK4SpringSimulation spring_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayRotater);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_DISPLAY_ROTATER_H_
