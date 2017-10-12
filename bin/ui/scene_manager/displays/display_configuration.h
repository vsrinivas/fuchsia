// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/displays/display_model.h"

namespace scene_manager {

// Automatically configures the display model based on available identifying
// characteristics.  These values can subsequently be overridden.
//
// |width_in_px| the width of the display in pixels.
// |height_in_px| the height of the display in pixels.
// |model| the model object to initialize, must not be null.
//
// TODO(MZ-16): This is a placeholder for more sophisticated configuration
// mechanisms we'll need in the future.
void ConfigureDisplay(uint32_t width_in_px,
                      uint32_t height_in_px,
                      DisplayModel* model);

}  // namespace scene_manager
