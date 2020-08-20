// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX355_CONSTANTS_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX355_CONSTANTS_H_

#include <cstdint>

#include "ddk/protocol/camera/sensor.h"

namespace camera {

constexpr uint8_t kByteMask = 0xFF;
constexpr uint8_t kByteShift = 8;
constexpr uint8_t kRaw10Bits = 10;
constexpr uint8_t kRaw12Bits = 12;
constexpr uint16_t kEndOfSequence = 0x0000;

constexpr uint16_t kSensorId = 0x0355;
constexpr uint16_t kSensorModelIdDefault = 0x0355;
constexpr uint8_t kNumTestPatternModes = 4;

constexpr uint32_t kAGainPrecision = 12;
constexpr uint32_t kDGainPrecision = 8;
constexpr uint16_t kDigitalGainShift = 8;
constexpr int32_t kLog2GainShift = 18;

constexpr int32_t kSensorExpNumber = 1;
constexpr uint32_t kMasterClock = 288000000;
// TODO(jsasinowski) Update the MaxIntegrationTime constants
constexpr uint32_t kMaxIntegrationTime =
    0x15BC;  // Max allowed for 30fps = 2782 (dec)=0x0ADE (hex) 15fps = 5564 (dec)=0x15BC (hex).
// Max allowed for 30fps = 2782 (dec)=0x0ADE (hex)
inline const uint32_t kMaxCoarseIntegrationTimeFor30fpsInLines = 0x0ADE;
// Max allowed for 15fps = 5564 (dec)=0x15BC (hex)
inline const uint32_t kMaxCoarseIntegrationTimeFor15fpsInLines = 0x15BC;
constexpr uint32_t kDefaultMaxIntegrationTimeInLines = kMaxCoarseIntegrationTimeFor15fpsInLines;

inline const std::array<frame_rate_info_t, EXTENSION_VALUE_ARRAY_LEN>
    frame_rate_to_integration_time_lut = {
        {
            {
                .frame_rate =
                    {
                        .frames_per_sec_numerator = 30,
                        .frames_per_sec_denominator = 1,
                    },
                .max_coarse_integration_time = kMaxCoarseIntegrationTimeFor30fpsInLines,
            },
            {
                .frame_rate =
                    {
                        .frames_per_sec_numerator = 15,
                        .frames_per_sec_denominator = 1,
                    },
                .max_coarse_integration_time = kMaxCoarseIntegrationTimeFor15fpsInLines,
            },
        },
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX355_CONSTANTS_H_
