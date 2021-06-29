// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_INPUT_DRIVERS_FOCALTECH_FT_FIRMWARE_H_
#define SRC_UI_INPUT_DRIVERS_FOCALTECH_FT_FIRMWARE_H_

#include <stddef.h>
#include <stdint.h>

namespace ft {

struct FirmwareEntry {
  uint8_t display_vendor;
  uint8_t ddic_version;
  const uint8_t* firmware_data;
  size_t firmware_size;
};

extern const FirmwareEntry kFirmwareEntries[];
extern const size_t kNumFirmwareEntries;

}  // namespace ft

#endif  // SRC_UI_INPUT_DRIVERS_FOCALTECH_FT_FIRMWARE_H_
