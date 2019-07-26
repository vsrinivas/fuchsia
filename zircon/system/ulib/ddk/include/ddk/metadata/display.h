// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_METADATA_DISPLAY_H_
#define DDK_METADATA_DISPLAY_H_

#include <zircon/types.h>

typedef struct {
  uint32_t vid;
  uint32_t pid;
  uint32_t did;
} display_driver_t;

#endif  // DDK_METADATA_DISPLAY_H_
