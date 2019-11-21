// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/storage/impl/clock_serialization.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "src/ledger/bin/storage/impl/clock_generated.h"
#include "src/ledger/bin/storage/impl/commit_serialization.h"
#include "src/ledger/bin/storage/public/constants.h"
#include "src/ledger/bin/storage/public/types.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/logging.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace storage {
namespace {
std::optional<flatbuffers::Offset<DeviceClockStorage>> SerializeDeviceClock(
    flatbuffers::FlatBufferBuilder* buffer, const clocks::DeviceId& device_id,
    const DeviceEntry& device_entry) {
  auto device_id_storage = CreateDeviceIdStorage(
      *buffer, convert::ToFlatBufferVector(buffer, device_id.fingerprint), device_id.epoch);
  flatbuffers::Offset<CommitIdAndGenerationStorage> head_storage =
      CreateCommitIdAndGenerationStorage(*buffer, ToIdStorage(device_entry.head.commit_id),
                                         device_entry.head.generation);
  flatbuffers::Offset<CommitIdAndGenerationStorage> cloud_storage;
  if (device_entry.cloud) {
    cloud_storage = CreateCommitIdAndGenerationStorage(
        *buffer, ToIdStorage(device_entry.cloud->commit_id), device_entry.cloud->generation);
  }
  return CreateDeviceClockStorage(
      *buffer, device_id_storage, DeviceEntryUnion_DeviceEntryStorage,
      CreateDeviceEntryStorage(*buffer, head_storage, cloud_storage).Union());
}

std::optional<flatbuffers::Offset<DeviceClockStorage>> SerializeDeviceClock(
    flatbuffers::FlatBufferBuilder* buffer, const clocks::DeviceId& device_id,
    const ClockTombstone& /*tombstone_entry*/) {
  auto device_id_storage = CreateDeviceIdStorage(
      *buffer, convert::ToFlatBufferVector(buffer, device_id.fingerprint), device_id.epoch);
  return CreateDeviceClockStorage(*buffer, device_id_storage, DeviceEntryUnion_Tombstone,
                                  CreateTombstone(*buffer).Union());
}

std::optional<flatbuffers::Offset<DeviceClockStorage>> SerializeDeviceClock(
    flatbuffers::FlatBufferBuilder* /*buffer*/, const clocks::DeviceId& /*device_id*/,
    const ClockDeletion& /*tombstone_entry*/) {
  return std::nullopt;
}
}  // namespace

std::string SerializeDeviceId(const clocks::DeviceId& device_id) {
  flatbuffers::FlatBufferBuilder buffer;
  auto storage = CreateDeviceIdStorage(
      buffer, convert::ToFlatBufferVector(&buffer, device_id.fingerprint), device_id.epoch);
  buffer.Finish(storage);
  return convert::ToString(buffer);
}

// Extracts a clocks::DeviceId from storage.
FXL_WARN_UNUSED_RESULT bool ExtractDeviceIdFromStorage(std::string data,
                                                       clocks::DeviceId* device_id) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(data.data()), data.size());
  if (!verifier.VerifyBuffer<DeviceIdStorage>(nullptr)) {
    return false;
  }

  const DeviceIdStorage* storage =
      flatbuffers::GetRoot<DeviceIdStorage>(reinterpret_cast<const unsigned char*>(data.data()));

  if (!storage || !storage->Verify(verifier) || !storage->fingerprint()) {
    return false;
  }

  device_id->fingerprint = convert::ToString(storage->fingerprint());
  device_id->epoch = storage->epoch();
  return true;
}

std::string SerializeClock(const Clock& clock) {
  flatbuffers::FlatBufferBuilder buffer;
  std::vector<flatbuffers::Offset<DeviceClockStorage>> device_clocks;
  for (const auto& [device_id, device_entry] : clock) {
    std::visit(
        [&buffer, &device_clocks, device_id = device_id](const auto& e) {
          auto device_clock = SerializeDeviceClock(&buffer, device_id, e);
          if (device_clock) {
            device_clocks.push_back(*device_clock);
          }
        },
        device_entry);
  }
  auto storage = CreateClockStorage(buffer, buffer.CreateVector(device_clocks));
  buffer.Finish(storage);
  return convert::ToString(buffer);
}

bool ExtractClockFromStorage(std::string data, Clock* clock) {
  flatbuffers::Verifier verifier(reinterpret_cast<const unsigned char*>(data.data()), data.size());
  if (!VerifyClockStorageBuffer(verifier)) {
    return false;
  }

  const ClockStorage* clock_storage =
      GetClockStorage(reinterpret_cast<const unsigned char*>(data.data()));

  if (!clock_storage || !clock_storage->Verify(verifier) || !clock_storage->devices()) {
    return false;
  }

  clock->clear();

  for (const auto& device_clock_storage : *clock_storage->devices()) {
    clocks::DeviceId device_id;
    if (!device_clock_storage->Verify(verifier) || !device_clock_storage->device_id() ||
        !device_clock_storage->device_id()->fingerprint()) {
      return false;
    }
    device_id.fingerprint = convert::ToString(device_clock_storage->device_id()->fingerprint());
    device_id.epoch = device_clock_storage->device_id()->epoch();

    switch (device_clock_storage->device_entry_type()) {
      case DeviceEntryUnion_DeviceEntryStorage: {
        const auto* device_entry_storage =
            static_cast<const DeviceEntryStorage*>(device_clock_storage->device_entry());
        if (!device_entry_storage || !device_entry_storage->Verify(verifier) ||
            !device_entry_storage->head() || !device_entry_storage->head()->commit_id()) {
          return false;
        }
        DeviceEntry device_entry;
        device_entry.head.commit_id =
            convert::ToString(ToCommitIdView(device_entry_storage->head()->commit_id()));
        device_entry.head.generation = device_entry_storage->head()->generation();

        if (device_entry_storage->cloud()) {
          if (!device_entry_storage->cloud()->commit_id()) {
            return false;
          }
          device_entry.cloud = {
              convert::ToString(ToCommitIdView(device_entry_storage->cloud()->commit_id())),
              device_entry_storage->cloud()->generation()};
        }
        clock->emplace(std::move(device_id), std::move(device_entry));
        break;
      }
      case DeviceEntryUnion_Tombstone: {
        clock->emplace(std::move(device_id), ClockTombstone());
        break;
      }
      case DeviceEntryUnion_NONE:
        // Should not happen.
        return false;
    }
  }
  return true;
}

}  // namespace storage
