// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddktl/mmio.h>
#include <fbl/optional.h>
#include "common.h"

namespace astro_display {

class Osd {
public:
    Osd(uint32_t fb_width, uint32_t fb_height, uint32_t display_width, uint32_t display_height)
        : fb_width_(fb_width), fb_height_(fb_height),
          display_width_(display_width), display_height_(display_height) {}

    zx_status_t Init(zx_device_t* parent);
    void HwInit();
    zx_status_t Configure();
    void Disable();
    void Flip(uint8_t idx);
    void Dump();
private:
    void DefaultSetup();
    // this function sets up scaling based on framebuffer and actual display
    // dimensions. The scaling IP and registers and undocumented.
    void EnableScaling(bool enable);
    void Enable();

    fbl::optional<ddk::MmioBuffer>      vpu_mmio_;
    pdev_protocol_t          pdev_ = {nullptr, nullptr};

    // Framebuffer dimension
    uint32_t                            fb_width_;
    uint32_t                            fb_height_;
    // Actual display dimension
    uint32_t                            display_width_;
    uint32_t                            display_height_;

    bool                                initialized_ = false;
};

} // namespace astro_display
