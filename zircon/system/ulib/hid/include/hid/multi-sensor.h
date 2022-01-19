// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HID_MULTI_SENSOR_H_
#define HID_MULTI_SENSOR_H_

#include <zircon/types.h>

__BEGIN_CDECLS

// clang-format off
#define ACCELEROMETER_RPT_ID_A     0x01
#define ACCELEROMETER_RPT_ID_B     0x02
#define GYROMETER_RPT_ID           0x03
#define COMPASS_RPT_ID             0x04
#define ILLUMINANCE_RPT_ID         0x05
// clang-format on

typedef struct accelerometer_input_rpt {
  uint8_t rpt_id;
  uint16_t x;
  uint16_t y;
  uint16_t z;
} __PACKED accelerometer_input_rpt_t;

typedef struct gyrometer_input_rpt {
  uint8_t rpt_id;
  uint16_t x;
  uint16_t y;
  uint16_t z;
} __PACKED gyrometer_input_rpt_t;

typedef struct compass_input_rpt {
  uint8_t rpt_id;
  uint16_t x;
  uint16_t y;
  uint16_t z;
} __PACKED compass_input_rpt_t;

typedef struct illuminance_input_rpt {
  uint8_t rpt_id;
  uint16_t illuminance;
} __PACKED illuminance_input_rpt_t;

size_t get_multi_sensor_report_desc(const uint8_t** buf);

__END_CDECLS

#endif  // HID_MULTI_SENSOR_H_
