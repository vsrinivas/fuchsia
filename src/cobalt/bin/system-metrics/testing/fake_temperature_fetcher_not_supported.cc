// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/testing/fake_temperature_fetcher_not_supported.h"

#include <stdint.h>

namespace cobalt {

FakeTemperatureFetcherNotSupported::FakeTemperatureFetcherNotSupported() {}

TemperatureFetchStatus FakeTemperatureFetcherNotSupported::FetchTemperature(int32_t *temperature) {
  return TemperatureFetchStatus::NOT_SUPPORTED;
}

}  // namespace cobalt
