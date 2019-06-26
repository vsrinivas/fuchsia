// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_SYSTEM_METRICS_TEMPERATURE_FETCHER_IMPL_H_
#define SRC_COBALT_BIN_SYSTEM_METRICS_TEMPERATURE_FETCHER_IMPL_H_

#include <lib/zx/channel.h>

#include <chrono>
#include <vector>

#include "src/cobalt/bin/system-metrics/temperature_fetcher.h"

using cobalt::TemperatureFetcher;

namespace cobalt {

class TemperatureFetcherImpl : public TemperatureFetcher {
 public:
  TemperatureFetcherImpl();
  bool FetchTemperature(uint32_t *temperature) override;

 private:
  zx_status_t GetDeviceHandle();

  zx::channel channel_;
};

}  // namespace cobalt

#endif  // SRC_COBALT_BIN_SYSTEM_METRICS_TEMPERATURE_FETCHER_IMPL_H_
