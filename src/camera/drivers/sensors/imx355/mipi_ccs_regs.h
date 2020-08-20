// Copyright 2020- The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX355_MIPI_CCS_REGS_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX355_MIPI_CCS_REGS_H_

// Definitions of sensor registers described by the MIPI CCS specification.

#include <zircon/types.h>

namespace camera {

constexpr uint16_t kSensorModelIdReg = 0x0016;
constexpr uint16_t kAnalogGainCodeMinReg = 0x0084;
constexpr uint16_t kAnalogGainCodeMaxReg = 0x0086;
constexpr uint16_t kAnalogGainCodeStepSizeReg = 0x0088;
constexpr uint16_t kAnalogGainM0Reg = 0x008c;
constexpr uint16_t kAnalogGainC0Reg = 0x008e;
constexpr uint16_t kAnalogGainM1Reg = 0x0090;
constexpr uint16_t kAnalogGainC1Reg = 0x0092;
constexpr uint16_t kModeSelectReg = 0x0100;
constexpr uint16_t kGroupedParameterHoldReg = 0x104;
constexpr uint16_t kCoarseIntegrationTimeReg = 0x202;
constexpr uint16_t kAnalogGainCodeGlobalReg = 0x0204;
constexpr uint16_t kDigitalGainGlobalReg = 0x020e;
constexpr uint16_t kFrameLengthLinesReg = 0x0340;
constexpr uint16_t kLineLengthPckReg = 0x0342;
constexpr uint16_t kOutputSizeXReg = 0x034c;
constexpr uint16_t kTestPatternReg = 0x0600;
constexpr uint16_t kDigitalGainMinReg = 0x1084;
constexpr uint16_t kDigitalGainMaxReg = 0x1086;
constexpr uint16_t kDigitalGainStepSizeReg = 0x1088;

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX355_MIPI_CCS_REGS_H_
