// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/presentation_switcher.h"

#include "garnet/bin/ui/root_presenter/presentation.h"

namespace root_presenter {

bool PresentationSwitcher::OnEvent(const fuchsia::ui::input::InputEvent& event,
                                   Presentation* presentation) {
  const fuchsia::ui::input::KeyboardEvent& kbd = event.keyboard();
  if (kbd.modifiers & fuchsia::ui::input::kModifierControl &&
      kbd.modifiers & fuchsia::ui::input::kModifierAlt &&
      kbd.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED) {
    if (kbd.code_point == 91 /* [ */) {
      presentation->yield_callback()(/* yield_to_next= */ false);
      return true;
    } else if (kbd.code_point == 93 /* ] */) {
      presentation->yield_callback()(/* yield_to_next= */ true);
      return true;
    }
  }
  return false;
}

}  // namespace root_presenter
