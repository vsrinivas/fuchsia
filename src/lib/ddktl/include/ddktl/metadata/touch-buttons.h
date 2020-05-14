// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_TOUCH_BUTTONS_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_TOUCH_BUTTONS_H_

#include <zircon/types.h>

// clang-format off
#define BUTTONS_ID_VOLUME_UP             0x00
#define BUTTONS_ID_VOLUME_DOWN           0x01
#define BUTTONS_ID_PLAY_PAUSE            0x02
// clang-format on

typedef struct {
  uint8_t id;   // e.g. BUTTONS_ID_VOLUME_UP.
  uint8_t idx;  // Matching index in the array of buttons.
} touch_button_config_t;

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_TOUCH_BUTTONS_H_
