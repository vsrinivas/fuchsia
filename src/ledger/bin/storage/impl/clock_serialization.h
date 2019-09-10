// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_BIN_STORAGE_IMPL_CLOCK_SERIALIZATION_H_
#define SRC_LEDGER_BIN_STORAGE_IMPL_CLOCK_SERIALIZATION_H_

#include <map>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/public/types.h"

namespace storage {

// Serializes a |ClockEntry| into a |data| string suitable for storage.
void SerializeClockEntry(const ClockEntry& entry, std::string* data);

// Extracts, from a set of serialized key/value pairs |entries| from storage, the list of known
// devices and their corresponding clock entry
FXL_WARN_UNUSED_RESULT bool ExtractClockFromStorage(
    std::vector<std::pair<std::string, std::string>> entries,
    std::map<DeviceId, ClockEntry>* clock);

}  // namespace storage

#endif  // SRC_LEDGER_BIN_STORAGE_IMPL_CLOCK_SERIALIZATION_H_
