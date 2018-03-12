// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <stdbool.h>
#include <stdint.h>

__BEGIN_CDECLS

#define PARADISE_RPT_ID_TOUCH 12
#define PARADISE_RPT_ID_STYLUS 6

#define PARADISE_FINGER_FLAGS_TSWITCH_MASK 0x01
#define PARADISE_FINGER_FLAGS_CONFIDENCE_MASK 0x04
#define paradise_finger_flags_tswitch(b) ((b) & PARADISE_FINGER_FLAGS_TSWITCH_MASK)
#define paradise_finger_flags_confidence(b) (!!(((b) & PARADISE_FINGER_FLAGS_CONFIDENCE_MASK) >> 2))

#define PARADISE_X_MAX 10368
#define PARADISE_Y_MAX 6912

typedef struct paradise_finger {
    uint8_t flags;
    uint16_t finger_id;

    uint16_t x;
    uint16_t y;
} __attribute__((packed)) paradise_finger_t;

typedef struct paradise_touch {
    uint8_t rpt_id;
    uint8_t pad;
    uint8_t contact_count;

    paradise_finger_t fingers[5];
    uint16_t scan_time;
} __attribute__((packed)) paradise_touch_t;

typedef struct paradise_finger_v2 {
    uint8_t flags;
    uint16_t finger_id;

    uint8_t width;
    uint8_t height;

    uint16_t x;
    uint16_t y;

    uint8_t pressure;
} __attribute__((packed)) paradise_finger_v2_t;

typedef struct paradise_touch_v2 {
    uint8_t rpt_id;
    uint8_t pad;
    uint8_t contact_count;

    paradise_finger_v2_t fingers[5];
    uint16_t scan_time;
} __attribute__((packed)) paradise_touch_v2_t;

#define PARADISE_STYLUS_STATUS_TSWITCH 0x01
#define PARADISE_STYLUS_STATUS_BARREL  0x02
#define PARADISE_STYLUS_STATUS_ERASER  0x04
#define PARADISE_STYLUS_STATUS_INVERT  0x08
#define PARADISE_STYLUS_STATUS_BARREL2 0x10
#define PARADISE_STYLUS_STATUS_INRANGE 0x20

#define paradise_stylus_status_tswitch(b) (b & PARADISE_STYLUS_STATUS_TSWITCH)
#define paradise_stylus_status_barrel(b)  (b & PARADISE_STYLUS_STATUS_BARREL)
#define paradise_stylus_status_eraser(b)  (b & PARADISE_STYLUS_STATUS_ERASER)
#define paradise_stylus_status_invert(b)  (b & PARADISE_STYLUS_STATUS_INVERT)
#define paradise_stylus_status_barrel2(b)  (b & PARADISE_STYLUS_STATUS_BARREL2)
#define paradise_stylus_status_inrange(b) (b & PARADISE_STYLUS_STATUS_INRANGE)

#define PARADISE_STYLUS_X_MAX 25919
#define PARADISE_STYLUS_Y_MAX 17279

typedef struct paradise_stylus {
    uint8_t rpt_id;
    uint8_t status;
    uint16_t x;
    uint16_t y;
    uint16_t pressure;
    uint16_t vendor_defined_ff00_5b;
    uint32_t tranducer_serial;
    uint8_t vendor_defined_ff00_00;
    uint8_t battery_strength;
    uint8_t x_tilt;
    uint8_t y_tilt;
    uint16_t scan_time;
} __attribute__((packed)) paradise_stylus_t;

bool is_paradise_touch_report_desc(const uint8_t* data, size_t len);
bool is_paradise_touch_v2_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_paradise_touch(int fd);

typedef struct paradise_touchpad_finger_v1 {
    uint8_t tip_switch : 1;
    uint8_t in_range : 7;

    uint16_t id;

    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t tip_pressure;
} __attribute((packed)) paradise_touchpad_finger_v1_t;

typedef struct paradise_touchpad_finger_v2 {
    uint8_t tip_switch : 1;
    uint8_t in_range : 7;

    uint16_t id;

    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
    uint8_t tip_pressure;
    uint16_t azimuth;
} __attribute((packed)) paradise_touchpad_finger_v2_t;

typedef struct paradise_touchpad_v1 {
    uint8_t report_id;

    uint8_t button : 1;
    uint8_t contact_count : 7;

    paradise_touchpad_finger_v1_t fingers[5];
} __attribute((packed)) paradise_touchpad_v1_t;

typedef struct paradise_touchpad_v2 {
    uint8_t report_id;

    uint8_t button : 1;
    uint8_t contact_count : 7;

    paradise_touchpad_finger_v2_t fingers[5];
} __attribute((packed)) paradise_touchpad_v2_t;

// The following typedef and function are transitional.
typedef paradise_touchpad_v2_t paradise_touchpad_t;
bool is_paradise_touchpad_report_desc(const uint8_t* data, size_t len);

bool is_paradise_touchpad_v1_report_desc(const uint8_t* data, size_t len);
bool is_paradise_touchpad_v2_report_desc(const uint8_t* data, size_t len);

zx_status_t setup_paradise_touchpad(int fd);

typedef struct paradise_sensor_vector_data {
  uint8_t sensor_num;
  int16_t vector[3];
} __attribute__((packed)) paradise_sensor_vector_data_t;

typedef struct paradise_sensor_scalar_data {
  uint8_t sensor_num;
  uint16_t scalar;
} __attribute__((packed)) paradise_sensor_scalar_data_t;

bool is_paradise_sensor_report_desc(const uint8_t* data, size_t len);
zx_status_t setup_paradise_sensor(int fd);

__END_CDECLS
