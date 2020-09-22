// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_ROOT_PRESENTER_DISPLAYS_DISPLAY_CONFIGURATION_H_
#define SRC_UI_BIN_ROOT_PRESENTER_DISPLAYS_DISPLAY_CONFIGURATION_H_

#include "src/ui/bin/root_presenter/displays/display_model.h"

namespace root_presenter {
namespace display_configuration {

// Automatically initializes the display model based on available identifying
// characteristics. These values can subsequently be overridden.
//
// |width_in_px| the width of the display in pixels.
// |height_in_px| the height of the display in pixels.
// |model| the model object to initialize, must not be null.
//
// TODO(fxbug.dev/23273): This is a placeholder for more sophisticated configuration
// mechanisms we'll need in the future.
void InitializeModelForDisplay(uint32_t width_in_px, uint32_t height_in_px, DisplayModel* model);

// Log the display metrics in debug mode.
void LogDisplayMetrics(const DisplayMetrics& metrics);

}  // namespace display_configuration
}  // namespace root_presenter

#endif  // SRC_UI_BIN_ROOT_PRESENTER_DISPLAYS_DISPLAY_CONFIGURATION_H_
