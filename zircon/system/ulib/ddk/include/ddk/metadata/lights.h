// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDK_METADATA_LIGHTS_H_
#define DDK_METADATA_LIGHTS_H_

#include <zircon/types.h>

typedef struct LightsConfig {
  bool brightness;
  bool rgb;
  bool init_on;
} lights_config_t;

#endif  // DDK_METADATA_LIGHTS_H_
