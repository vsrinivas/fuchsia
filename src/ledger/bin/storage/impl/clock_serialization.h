// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_CLOCK_SERIALIZATION_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_CLOCK_SERIALIZATION_H_

#include <map>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/public/types.h"
#include "third_party/abseil-cpp/absl/base/attributes.h"

namespace storage {

// Serializes a |clocks::DeviceId| into a |data| string suitable for storage.
std::string SerializeDeviceId(const clocks::DeviceId& device_id);

// Extracts a clocks::DeviceId from storage.
ABSL_MUST_USE_RESULT bool ExtractDeviceIdFromStorage(std::string data, clocks::DeviceId* device_id);

// Serializes a |Clock| into a |data| string suitable for storage.
std::string SerializeClock(const Clock& entry);

// Extracts from the clock entry in storage, the list of known devices and their corresponding clock
// entry
ABSL_MUST_USE_RESULT bool ExtractClockFromStorage(std::string data, Clock* clock);

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_CLOCK_SERIALIZATION_H_
