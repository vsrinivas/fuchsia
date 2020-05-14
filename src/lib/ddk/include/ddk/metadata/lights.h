// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDK_INCLUDE_DDK_METADATA_LIGHTS_H_
#define SRC_LIB_DDK_INCLUDE_DDK_METADATA_LIGHTS_H_

#include <zircon/types.h>

typedef struct LightsConfig {
  bool brightness;
  bool rgb;
  bool init_on;
  int32_t group_id;
} lights_config_t;

#endif  // SRC_LIB_DDK_INCLUDE_DDK_METADATA_LIGHTS_H_
