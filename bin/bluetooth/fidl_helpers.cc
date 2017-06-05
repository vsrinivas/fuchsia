// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl_helpers.h"

#include <unordered_set>

#include <endian.h>

#include "apps/bluetooth/lib/common/uuid.h"
#include "apps/bluetooth/lib/gap/advertising_data.h"

// The internal library components and the generated FIDL bindings are both declared under the
// "bluetooth" namespace. We define an alias here to disambiguate.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {
namespace fidl_helpers {
namespace {

bool PopulateUUIDs(const ::bluetooth::common::BufferView& data, size_t uuid_size,
                   std::unordered_set<std::string>* to_populate) {
  FTL_DCHECK(to_populate);

  if (data.GetSize() % uuid_size) {
    FTL_LOG(WARNING) << "Malformed service UUIDs list";
    return false;
  }

  size_t uuid_count = data.GetSize() / uuid_size;
  for (size_t i = 0; i < uuid_count; i++) {
    const ::bluetooth::common::BufferView uuid_bytes(data.GetData() + i * uuid_size, uuid_size);
    ::bluetooth::common::UUID uuid;
    ::bluetooth::common::UUID::FromBytes(uuid_bytes, &uuid);

    to_populate->insert(uuid.ToString());
  }

  return true;
}

::btfidl::control::TechnologyType TechnologyTypeToFidl(::bluetooth::gap::TechnologyType type) {
  switch (type) {
    case ::bluetooth::gap::TechnologyType::kLowEnergy:
      return ::btfidl::control::TechnologyType::LOW_ENERGY;
    case ::bluetooth::gap::TechnologyType::kClassic:
      return ::btfidl::control::TechnologyType::CLASSIC;
    case ::bluetooth::gap::TechnologyType::kDualMode:
      return ::btfidl::control::TechnologyType::DUAL_MODE;
    default:
      FTL_NOTREACHED();
      break;
  }

  // This should never execute.
  return ::btfidl::control::TechnologyType::DUAL_MODE;
}

}  // namespace

::btfidl::StatusPtr NewErrorStatus(::bluetooth::ErrorCode error_code,
                                   const std::string& description) {
  auto status = ::btfidl::Status::New();
  status->error = ::btfidl::Error::New();
  status->error->error_code = error_code;
  status->error->description = description;

  return status;
}

::btfidl::control::AdapterInfoPtr NewAdapterInfo(const ::bluetooth::gap::Adapter& adapter) {
  auto adapter_info = ::btfidl::control::AdapterInfo::New();
  adapter_info->state = ::btfidl::control::AdapterState::New();

  // TODO(armansito): Most of these fields have not been implemented yet. Assign the correct values
  // when they are supported.
  adapter_info->state->powered = ::btfidl::Bool::New();
  adapter_info->state->powered->value = true;
  adapter_info->state->discovering = ::btfidl::Bool::New();
  adapter_info->state->discoverable = ::btfidl::Bool::New();

  adapter_info->identifier = adapter.identifier();
  adapter_info->address = adapter.state().controller_address().ToString();

  return adapter_info;
}

::btfidl::control::RemoteDevicePtr NewRemoteDevice(const ::bluetooth::gap::RemoteDevice& device) {
  auto fidl_device = ::btfidl::control::RemoteDevice::New();
  fidl_device->identifier = device.identifier();
  fidl_device->address = device.address().value().ToString();
  fidl_device->technology = TechnologyTypeToFidl(device.technology());

  // TODO(armansito): Report correct values once we support these.
  fidl_device->connected = false;
  fidl_device->bonded = false;

  // Set default value for device appearance.
  fidl_device->appearance = ::btfidl::control::Appearance::UNKNOWN;

  if (device.rssi() != ::bluetooth::hci::kRSSIInvalid) {
    auto fidl_rssi = ::btfidl::Int8::New();
    fidl_rssi->value = device.rssi();
    fidl_device->rssi = std::move(fidl_rssi);
  }

  ::bluetooth::gap::AdvertisingDataReader reader(device.advertising_data());

  // Advertising data that made it this far is guaranteed to be valid as invalid data would not pass
  // the filters.
  FTL_DCHECK(reader.is_valid());

  std::unordered_set<std::string> uuids;

  ::bluetooth::gap::DataType type;
  ::bluetooth::common::BufferView data;
  while (reader.GetNextField(&type, &data)) {
    switch (type) {
      case ::bluetooth::gap::DataType::kTxPowerLevel: {
        // Data must contain exactly one octet.
        if (data.GetSize() != 1) {
          FTL_LOG(WARNING) << "Received malformed Tx Power Level";
          return nullptr;
        }

        auto fidl_tx_power = ::btfidl::Int8::New();
        fidl_tx_power->value = static_cast<int8_t>(data.GetData()[0]);
        fidl_device->tx_power = std::move(fidl_tx_power);
        break;
      }
      case ::bluetooth::gap::DataType::kShortenedLocalName:
        // If a name has been previously set (e.g. because the Complete Local Name was included in
        // the scan response) then break. Otherwise we fall through.
        if (fidl_device->name) break;
      case ::bluetooth::gap::DataType::kCompleteLocalName:
        fidl_device->name = data.ToString();
        break;
      case ::bluetooth::gap::DataType::kAppearance: {
        // TODO(armansito): RemoteDevice should have a function to return the device appearance, as
        // it can be obtained either from advertising data or via GATT.

        // Data must contain exactly two octets.
        if (data.GetSize() != 2) {
          FTL_LOG(WARNING) << "Received malformed Appearance";
          return nullptr;
        }

        fidl_device->appearance = static_cast<::btfidl::control::Appearance>(
            le16toh(*reinterpret_cast<const uint16_t*>(data.GetData())));

        break;
      }
      case ::bluetooth::gap::DataType::kIncomplete16BitServiceUUIDs:
      case ::bluetooth::gap::DataType::kComplete16BitServiceUUIDs:
        if (!PopulateUUIDs(data, 2, &uuids)) return nullptr;
        break;
      case ::bluetooth::gap::DataType::kIncomplete32BitServiceUUIDs:
      case ::bluetooth::gap::DataType::kComplete32BitServiceUUIDs:
        if (!PopulateUUIDs(data, 4, &uuids)) return nullptr;
        break;
      case ::bluetooth::gap::DataType::kIncomplete128BitServiceUUIDs:
      case ::bluetooth::gap::DataType::kComplete128BitServiceUUIDs:
        if (!PopulateUUIDs(data, 16, &uuids)) return nullptr;
        break;
      default:
        break;
    }
  }

  // |service_uuids| is not a nullable field, so we need to assign something to it.
  if (uuids.empty()) {
    fidl_device->service_uuids.resize(0);
  } else {
    for (const auto& uuid : uuids) {
      fidl_device->service_uuids.push_back(uuid);
    }
  }

  return fidl_device;
}

}  // namespace fidl_helpers
}  // namespace bluetooth_service
