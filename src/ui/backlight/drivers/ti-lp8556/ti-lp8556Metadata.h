// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BACKLIGHT_DRIVERS_TI_LP8556_TI_LP8556METADATA_H_
#define SRC_UI_BACKLIGHT_DRIVERS_TI_LP8556_TI_LP8556METADATA_H_

#include <stdint.h>

struct TiLp8556Metadata {
  uint8_t panel_id;
  bool allow_set_current_scale;
  uint8_t registers[256 * 2];
  uint32_t register_count;  // Refers to the number of both registers and values listed in the
                            // registers field.
};

#endif  // SRC_UI_BACKLIGHT_DRIVERS_TI_LP8556_TI_LP8556METADATA_H_
