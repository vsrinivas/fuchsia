// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_PREFERRED_SCANOUT_IMAGE_TYPE_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_PREFERRED_SCANOUT_IMAGE_TYPE_H_

#include <cstdint>

// display-controller.fidl states that TYPE_SIMPLE is the only universally-supported image type
// defined by the API, and that any other value must be agreed upon by all parties (e.g. the image
// producer, the display driver, etc.) through some other means, perhaps a future negotiation API.
// For now, this header serves the role of "some other means".

#if defined(__x86_64__)
// IMAGE_TYPE_X_TILED from ddk/protocol/intelgpucore.h
constexpr uint32_t IMAGE_TYPE_PREFERRED_SCANOUT = 1;
#elif defined(__aarch64__)
// TYPE_SIMPLE from fuchsia.hardware.display/display-controller.fidl
// ImageType::SIMPLE from display-controller.banjo
constexpr uint32_t IMAGE_TYPE_PREFERRED_SCANOUT = 0;
#else
#error "Preferred scanout image format only defined on Intel and ARM."
#endif

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_DISPLAY_PREFERRED_SCANOUT_IMAGE_TYPE_H_
