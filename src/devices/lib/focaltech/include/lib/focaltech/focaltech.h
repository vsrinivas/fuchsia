// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_FOCALTECH_INCLUDE_LIB_FOCALTECH_FOCALTECH_H_
#define SRC_DEVICES_LIB_FOCALTECH_INCLUDE_LIB_FOCALTECH_FOCALTECH_H_

#include <stdbool.h>
#include <stdint.h>

#define FOCALTECH_DEVICE_FT3X27 0
#define FOCALTECH_DEVICE_FT6336 1
#define FOCALTECH_DEVICE_FT5726 2

struct FocaltechMetadata {
  uint32_t device_id;      // The specific FocalTech IC, must be a FOCALTECH_DEVICE_ value.
  bool needs_firmware;     // Whether or not to use the next two fields.
  uint8_t display_vendor;  // The platform-specific display vendor ID.
  uint8_t ddic_version;    // The platform-specific DDIC version ID.
};

#endif  // SRC_DEVICES_LIB_FOCALTECH_INCLUDE_LIB_FOCALTECH_FOCALTECH_H_
