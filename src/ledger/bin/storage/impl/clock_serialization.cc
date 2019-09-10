// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/clock_serialization.h"

#include <map>
#include <string>
#include <vector>

#include "peridot/lib/convert/convert.h"
#include "src/ledger/bin/storage/impl/clock_generated.h"
#include "src/ledger/bin/storage/impl/commit_serialization.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace storage {
void SerializeClockEntry(const ClockEntry& entry, std::string* data) {
  FXL_DCHECK(entry.commit_id.size() == kCommitIdSize);

  flatbuffers::FlatBufferBuilder builder;
  auto storage = CreateClockEntryStorage(builder, ToIdStorage(entry.commit_id), entry.generation);
  builder.Finish(storage);
  *data = convert::ToString(builder);
}

bool ExtractClockFromStorage(std::vector<std::pair<std::string, std::string>> entries,
                             std::map<DeviceId, ClockEntry>* clock) {
  for (auto& [device_id_bytes, clock_entry_bytes] : entries) {
    flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(clock_entry_bytes.data()),
                                   clock_entry_bytes.size());
    if (!VerifyClockEntryStorageBuffer(verifier)) {
      return false;
    }

    const ClockEntryStorage* storage =
        GetClockEntryStorage(reinterpret_cast<const unsigned char*>(clock_entry_bytes.data()));

    CommitIdView commit_id_view = ToCommitIdView(storage->commit_id());
    ClockEntry clock_entry{commit_id_view.ToString(), storage->generation()};
    (*clock)[std::move(device_id_bytes)] = std::move(clock_entry);
  }
  return true;
}

}  // namespace storage
