// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_METADATA_BUTTONS_H_
#define SRC_LIB_DDK_INCLUDE_DDK_METADATA_BUTTONS_H_

#include <zircon/types.h>

// TODO(andresoportus): Move to C++/ddktl once astro board driver is converted to C++.

// clang-format off
#define BUTTONS_ID_VOLUME_UP             0x00
#define BUTTONS_ID_VOLUME_DOWN           0x01
#define BUTTONS_ID_FDR                   0x02
#define BUTTONS_ID_MIC_MUTE              0x03
#define BUTTONS_ID_PLAY_PAUSE            0x04
#define BUTTONS_ID_KEY_A                 0x05
#define BUTTONS_ID_KEY_M                 0x06
#define BUTTONS_ID_CAM_MUTE              0x07
#define BUTTONS_ID_MAX                   0x08

#define BUTTONS_TYPE_DIRECT              0x00
#define BUTTONS_TYPE_MATRIX              0x01

#define BUTTONS_GPIO_TYPE_INTERRUPT      0x01
#define BUTTONS_GPIO_TYPE_MATRIX_OUTPUT  0x02

#define BUTTONS_GPIO_FLAG_INVERTED       0x80
// clang-format on

typedef struct ButtonConfig {
  uint8_t type;              // e.g. BUTTONS_TYPE_DIRECT.
  uint8_t id;                // e.g. BUTTONS_ID_VOLUME_UP.
  uint8_t gpioA_idx;         // For BUTTONS_TYPE_DIRECT only gpioA used and must be
                             // BUTTONS_GPIO_TYPE_INTERRUPT.
  uint8_t gpioB_idx;         // For BUTTONS_TYPE_MATRIX gpioB (column) must be
                             // BUTTONS_GPIO_TYPE_MATRIX_OUTPUT (is driven most of the time) and
                             // gpioA (row) must be BUTTONS_GPIO_TYPE_INTERRUPT (triggers an
                             // interrupt most of the time).
                             // During matrix scans columns are floated and rows are read.
  zx_duration_t gpio_delay;  // For settling during matrix scan.
} buttons_button_config_t;

typedef struct GpioConfig {
  uint8_t type;   // e.g. BUTTONS_GPIO_TYPE_INTERRUPT.
  uint8_t flags;  // e.g. BUTTONS_GPIO_FLAG_INVERTED.
  union {
    uint32_t internal_pull;  // Only applicable to BUTTONS_GPIO_TYPE_INTERRUPT.
    uint8_t output_value;    // Only applicable to BUTTONS_GPIO_TYPE_MATRIX_OUTPUT.
  };
} buttons_gpio_config_t;

#endif  // SRC_LIB_DDK_INCLUDE_DDK_METADATA_BUTTONS_H_
