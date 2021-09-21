// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_LIGHT_SENSOR_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_LIGHT_SENSOR_H_

namespace metadata {

struct LightSensorParams {
  uint8_t gain;
  uint32_t integration_time_us;
  // The polling time in milliseconds. 0 is defined as no polling.
  uint32_t polling_time_us;
};

}  // namespace metadata

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_METADATA_LIGHT_SENSOR_H_
