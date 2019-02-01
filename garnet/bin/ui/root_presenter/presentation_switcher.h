// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_SWITCHER_H_
#define GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_SWITCHER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include "lib/ui/scenic/cpp/resources.h"

namespace root_presenter {

class Presentation;

class PresentationSwitcher final {
 public:
  // Switch the active presentation to the previous one on Ctrl+Alt+[, and to
  // the next one on Ctrl+Alt+].
  bool OnEvent(const fuchsia::ui::input::InputEvent& event,
               Presentation* presentation);
};

}  // namespace root_presenter

#endif  // GARNET_BIN_UI_ROOT_PRESENTER_PRESENTATION_SWITCHER_H_
