// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "escher/escher.h"
#include "escher/forward_declarations.h"

namespace escher {

// Return RGBA pixels containing a checkerboard pattern, where each white/black
// region is a single pixel.
std::unique_ptr<uint8_t[]> NewCheckerboardPixels(uint32_t width,
                                                 uint32_t height);

// Return RGBA pixels containing random noise.
std::unique_ptr<uint8_t[]> NewNoisePixels(uint32_t width, uint32_t height);

// Create new Image, populating it with pixels from NewCheckerboardPixels().
ImagePtr NewCheckerboardImage(Escher* escher, uint32_t width, uint32_t height);

// Create new Image, populating it with pixels from NewNoisePixels().
ImagePtr NewNoiseImage(Escher* escher, uint32_t width, uint32_t height);

}  // namespace escher
