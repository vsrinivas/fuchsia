// Copyright 2019  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/testing/fake_temperature_fetcher.h"

#include <stdint.h>

namespace cobalt {

FakeTemperatureFetcher::FakeTemperatureFetcher() {}

bool FakeTemperatureFetcher::FetchTemperature(uint32_t *temperature) {
  *temperature = 38;
  return true;
}

}  // namespace cobalt
