// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX355_IMX355_MODES_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX355_IMX355_MODES_H_

#include <array>

#include "ddk/protocol/camera.h"
#include "ddk/protocol/camera/sensor.h"

namespace camera {

const uint8_t kAvailableModesArraySize = 1;

const uint8_t kSENSOR_IMX355_SEQ_DEFAULT_PREVIEW = 0;

constexpr std::array<operating_mode_t, kAvailableModesArraySize> available_modes = {{
    {
        .idx = kSENSOR_IMX355_SEQ_DEFAULT_PREVIEW,
        .identifier = "imx355_default_preview",
        // NOTE: SW reference consumes this as (30fps * 256)
        //       We are representing this as fpms.
        //       Take account of the multiplier when needed.
        .fps = 30,
        .resolution_in =
            {
                .x = 3280,
                .y = 2464,
            },
        .resolution_out =
            {
                .x = 3280,
                .y = 2464,
            },
        .exposures = 1,
        .dr_mode = DYNAMIC_RANGE_MODE_LINEAR,
        .pixel_type = PIXEL_TYPE_RAW10,
        .lanes = 4,
        .mbps = 720,
        // The below params are not used.
        .line_time = 0,
        .frame_time = 0,
        .readout = {},
    },
}};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX355_IMX355_MODES_H_
