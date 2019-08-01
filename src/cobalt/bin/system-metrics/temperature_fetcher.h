// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_TEMPERATURE_FETCHER_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_TEMPERATURE_FETCHER_H_

#include <stdint.h>

namespace cobalt {

enum TemperatureFetchStatus {
  SUCCEED,
  FAIL,
  NOT_SUPPORTED,
};

// An abstract interface for temperature fetching from various
// resources
class TemperatureFetcher {
 public:
  virtual ~TemperatureFetcher() = default;

  // Temperature in degrees Celsius.
  virtual TemperatureFetchStatus FetchTemperature(int32_t* temperature) = 0;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_TEMPERATURE_FETCHER_H_
