// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <unistd.h>
#include <ddktl/device.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/i2c.h>
#include "common.h"

namespace astro_display {

class Backlight {
public:
    Backlight() {}
    zx_status_t Init(zx_device_t* parent);
    void Enable();
    void Disable();

private:
    gpio_protocol_t                 gpio_ = {};
    i2c_protocol_t                  i2c_ = {};

    bool                            initialized_ = false;
    bool                            enabled_ = false;
};
} // namespace astro_display
