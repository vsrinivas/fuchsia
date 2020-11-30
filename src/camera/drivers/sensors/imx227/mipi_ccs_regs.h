// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX227_MIPI_CCS_REGS_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX227_MIPI_CCS_REGS_H_

// Definitions of sensor registers described by the MIPI CCS specification.

#include <zircon/types.h>

namespace camera {

constexpr uint16_t kSensorModelIdReg = 0x0016;

struct __PACKED Imx227AnalogGainRegisters {
  static constexpr uint16_t kBaseAddress = 0x0084;

  // 0x0084
  uint16_t code_min;
  // 0x0086
  uint16_t code_max;
  // 0x0088
  uint16_t code_step;
  // 0x008a
  uint16_t gain_type;
  // 0x008c
  uint16_t m0;
  // 0x008e
  uint16_t c0;
  // 0x0090
  uint16_t m1;
  // 0x0092
  uint16_t c1;
};

struct __PACKED Imx227DigitalGainRegisters {
  static constexpr uint16_t kBaseAddress = 0x1084;

  // 0x1084
  uint16_t gain_min;
  // 0x1086
  uint16_t gain_max;
  // 0x1088
  uint16_t gain_step_size;
};

constexpr uint16_t kModeSelectReg = 0x0100;
constexpr uint16_t kGroupedParameterHoldReg = 0x104;
constexpr uint16_t kTempCtrlReg = 0x0138;
constexpr uint16_t kTempOutputReg = 0x013A;
constexpr uint16_t kCoarseIntegrationTimeReg = 0x202;
constexpr uint16_t kAnalogGainCodeGlobalReg = 0x0204;
constexpr uint16_t kDigitalGainGlobalReg = 0x020e;
constexpr uint16_t kFrameLengthLinesReg = 0x0340;
constexpr uint16_t kLineLengthPckReg = 0x0342;
constexpr uint16_t kTestPatternReg = 0x0600;

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX227_MIPI_CCS_REGS_H_
