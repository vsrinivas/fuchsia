// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_MODES_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_MODES_H_

#include <fuchsia/hardware/camera/c/banjo.h>
#include <fuchsia/hardware/camera/sensor/c/banjo.h>

#include <array>

namespace camera {

const uint8_t kAvailableModesArraySize = 5;

const uint8_t kSENSOR_IMX227_SEQ_DEFAULT_PREVIEW = 0;
const uint8_t kSENSOR_IMX227_SEQ_TESTPATTERN = 1;
const uint8_t kSENSOR_IMX227_SEQ_TESTPATTERN_960M = 2;
const uint8_t kSENSOR_IMX227_SEQ_1080P_TESTPATTERN = 3;
const uint8_t kSENSOR_IMX227_SEQ_1080P_PREVIEW = 4;
const uint8_t kSENSOR_IMX227_SEQ_DEFAULT_FULLSENSOR_PREVIEW = 5;
const uint8_t kSENSOR_IMX227_SEQ_TESTPATTERN_SOLID_COLORBARS = 6;

constexpr std::array<operating_mode_t, kAvailableModesArraySize> available_modes = {{
    {
        .idx = kSENSOR_IMX227_SEQ_DEFAULT_PREVIEW,
        .identifier = "imx227_default_preview",
        // NOTE: SW reference consumes this as (30fps * 256)
        //       We are representing this as fpms.
        //       Take account of the multiplier when needed.
        .fps = 30,
        .resolution_in =
            {
                .x = 2200,
                .y = 2720,
            },
        .resolution_out =
            {
                .x = 2200,
                .y = 2720,
            },
        .exposures = 1,
        .dr_mode = DYNAMIC_RANGE_MODE_LINEAR,
        .pixel_type = PIXEL_TYPE_RAW10,
        .lanes = 2,
        .mbps = 1000,
        // The below params are not used.
        .line_time = 0,
        .frame_time = 0,
        .readout = {},
    },
    {
        .idx = kSENSOR_IMX227_SEQ_1080P_PREVIEW,
        .identifier = "imx227_1080p_preview",
        // NOTE: SW reference consumes this as (30fps * 256)
        //       We are representing this as fpms.
        //       Take account of the multiplier when needed.
        .fps = 30,
        .resolution_in =
            {
                .x = 1920,
                .y = 1080,
            },
        .resolution_out =
            {
                .x = 1920,
                .y = 1080,
            },
        .exposures = 1,
        .dr_mode = DYNAMIC_RANGE_MODE_LINEAR,
        .pixel_type = PIXEL_TYPE_RAW10,
        .lanes = 2,
        .mbps = 1000,
        // The below params are not used.
        .line_time = 0,
        .frame_time = 0,
        .readout = {},
    },
    {
        .idx = kSENSOR_IMX227_SEQ_DEFAULT_FULLSENSOR_PREVIEW,
        .identifier = "imx227_fullsensor_preview",
        // NOTE: SW reference consumes this as (28fps * 256)
        //       We are representing this as fpms.
        //       Take account of the multiplier when needed.
        .fps = 28,
        .resolution_in =
            {
                .x = 2400,
                .y = 2720,
            },
        .resolution_out =
            {
                .x = 2400,
                .y = 2720,
            },
        .exposures = 1,
        .dr_mode = DYNAMIC_RANGE_MODE_LINEAR,
        .pixel_type = PIXEL_TYPE_RAW10,
        .lanes = 2,
        .mbps = 1000,
        // The below params are not used.
        .line_time = 0,
        .frame_time = 0,
        .readout = {},
    },
    {
        .idx = kSENSOR_IMX227_SEQ_TESTPATTERN,
        .identifier = "imx227_testpattern",
        // NOTE: SW reference consumes this as (30fps * 256)
        //       We are representing this as fpms.
        //       Take account of the multiplier when needed.
        .fps = 30,
        .resolution_in =
            {
                .x = 2200,
                .y = 2720,
            },
        .resolution_out =
            {
                .x = 2200,
                .y = 2720,
            },
        .exposures = 1,
        .dr_mode = DYNAMIC_RANGE_MODE_LINEAR,
        .pixel_type = PIXEL_TYPE_RAW10,
        .lanes = 2,
        .mbps = 1000,
        // The below params are not used.
        .line_time = 0,
        .frame_time = 0,
        .readout = {},
    },
    {
        .idx = kSENSOR_IMX227_SEQ_TESTPATTERN_SOLID_COLORBARS,
        .identifier = "imx227_testpattern_solid_colorbars",
        // NOTE: SW reference consumes this as (30fps * 256)
        //       We are representing this as fpms.
        //       Take account of the multiplier when needed.
        .fps = 30,
        .resolution_in =
            {
                .x = 2200,
                .y = 2720,
            },
        .resolution_out =
            {
                .x = 2200,
                .y = 2720,
            },
        .exposures = 1,
        .dr_mode = DYNAMIC_RANGE_MODE_LINEAR,
        .pixel_type = PIXEL_TYPE_RAW10,
        .lanes = 2,
        .mbps = 1000,
        // The below params are not used.
        .line_time = 0,
        .frame_time = 0,
        .readout = {},
    },
}};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_MODES_H_
