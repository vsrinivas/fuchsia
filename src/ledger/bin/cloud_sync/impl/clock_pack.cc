// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/cloud_sync/impl/clock_pack.h"

#include <optional>

#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/encoding/encoding.h"

namespace cloud_sync {
namespace {
void AddToClock(const storage::DeviceEntry& entry, cloud_provider::DeviceClock* clock) {
  cloud_provider::DeviceEntry device_entry;
  cloud_provider::ClockEntry clock_entry;
  clock_entry.set_commit_id(convert::ToArray(entry.head.commit_id));
  clock_entry.set_generation(entry.head.generation);
  device_entry.set_local_entry(std::move(clock_entry));
  clock->set_device_entry(std::move(device_entry));
}

void AddToClock(const storage::ClockTombstone& /*entry*/, cloud_provider::DeviceClock* clock) {
  cloud_provider::DeviceEntry device_entry;
  cloud_provider::TombstoneEntry tombstone;
  device_entry.set_tombstone_entry(std::move(tombstone));
  clock->set_device_entry(std::move(device_entry));
}

void AddToClock(const storage::ClockDeletion& entry, cloud_provider::DeviceClock* clock) {
  cloud_provider::DeviceEntry device_entry;
  cloud_provider::DeletionEntry deletion;
  device_entry.set_deletion_entry(std::move(deletion));
  clock->set_device_entry(std::move(device_entry));
}

}  // namespace

cloud_provider::ClockPack EncodeClock(const storage::Clock& clock) {
  cloud_provider::Clock clock_p;
  for (const auto& [device_id, device_clock] : clock) {
    cloud_provider::DeviceClock device_clock_p;
    device_clock_p.set_fingerprint(convert::ToArray(device_id.fingerprint));
    device_clock_p.set_counter(device_id.epoch);
    std::visit([&device_clock_p](const auto& entry) { AddToClock(entry, &device_clock_p); },
               device_clock);
    clock_p.mutable_devices()->push_back(std::move(device_clock_p));
  }
  cloud_provider::ClockPack pack;
  ledger::EncodeToBuffer(&clock_p, &pack.buffer);
  return pack;
}

bool DecodeClock(cloud_provider::ClockPack clock_pack, storage::Clock* clock) {
  cloud_provider::Clock unpacked_clock;
  if (!ledger::DecodeFromBuffer(clock_pack.buffer, &unpacked_clock)) {
    FXL_LOG(ERROR) << "Unable to decode from buffer";
    return false;
  }
  storage::Clock result;
  if (unpacked_clock.has_devices()) {
    for (const auto& dv : unpacked_clock.devices()) {
      if (!dv.has_fingerprint() || !dv.has_counter() || !dv.has_device_entry()) {
        FXL_LOG(ERROR) << "Missing elements" << dv.has_fingerprint() << " " << dv.has_counter()
                       << " " << dv.has_device_entry();
        return false;
      }
      clocks::DeviceId device_id;
      device_id.epoch = dv.counter();
      device_id.fingerprint = convert::ToString(dv.fingerprint());

      storage::DeviceClock device_clock;
      switch (dv.device_entry().Which()) {
        case cloud_provider::DeviceEntry::Tag::kLocalEntry: {
          const cloud_provider::ClockEntry& entry = dv.device_entry().local_entry();
          storage::DeviceEntry device_entry;
          device_entry.head =
              storage::ClockEntry{convert::ToString(entry.commit_id()), entry.generation()};
          device_entry.cloud = std::make_optional(device_entry.head);
          device_clock.emplace<storage::DeviceEntry>(std::move(device_entry));
          break;
        }
        case cloud_provider::DeviceEntry::Tag::kTombstoneEntry: {
          device_clock.emplace<storage::ClockTombstone>();
          break;
        }
        case cloud_provider::DeviceEntry::Tag::kDeletionEntry: {
          device_clock.emplace<storage::ClockDeletion>();
          break;
        }
        case cloud_provider::DeviceEntry::Tag::kUnknown: {
          return false;
          break;
        }
      }
      result.emplace(std::move(device_id), std::move(device_clock));
    }
  }
  *clock = result;
  return true;
}

}  // namespace cloud_sync
