// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DDKTL_METADATA_LIGHT_SENSOR_H_
#define DDKTL_METADATA_LIGHT_SENSOR_H_

namespace metadata {

struct LightSensorParams {
  uint8_t gain;
  uint32_t integration_time_ms;
};

}  // namespace metadata

#endif  // DDKTL_METADATA_LIGHT_SENSOR_H_
