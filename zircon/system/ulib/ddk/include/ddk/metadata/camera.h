// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_METADATA_CAMERA_H_
#define DDK_METADATA_CAMERA_H_

#include <zircon/types.h>

typedef struct {
  uint32_t vid;
  uint32_t pid;
  uint32_t did;
} camera_sensor_t;

typedef struct {
  uint32_t vid;
  uint32_t pid;
  uint32_t did;
} mipi_adapter_t;

#endif  // DDK_METADATA_CAMERA_H_
