// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/impl/clock_serialization.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* Data, size_t Size) {
  std::string bytes(reinterpret_cast<const char*>(Data), Size);

  clocks::DeviceId device_id;
  if (storage::ExtractDeviceIdFromStorage(bytes, &device_id)) {
    return 0;
  }
  return 0;
}
