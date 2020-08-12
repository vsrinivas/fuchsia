// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_ID_H_
#define SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_ID_H_

// clang-format off
#define TOTAL_RESOLUTION              0x0001
#define ACTIVE_RESOLUTION             0x0002
#define PIXELS_PER_LINE               0x0003
#define AGAIN_LOG2_MAX                0x0004
#define DGAIN_LOG2_MAX                0x0005
#define AGAIN_ACCURACY                0x0006
#define INT_TIME_MIN                  0x0007
#define INT_TIME_MAX                  0x0008
#define INT_TIME_LONG_MAX             0x0009
#define INT_TIME_LIMIT                0x000A
#define DAY_LIGHT_INT_TIME_MAX        0x000B
#define INT_TIME_APPLY_DELAY          0x000C
#define ISP_EXPOSURE_CHANNEL_DELAY    0x000D
#define X_OFFSET                      0x000E
#define Y_OFFSET                      0x000F
#define LINES_PER_SECOND              0x0010
#define SENSOR_EXP_NUMBER             0x0011
#define MODE                          0x0012
#define FRAME_RATE_COARSE_INT_LUT     0x0013
#define TEMP                          0x0014
// clang-format on

#endif  // SRC_CAMERA_DRIVERS_SENSORS_IMX227_IMX227_ID_H_
