// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl_helpers.h"

#include <unordered_set>

#include <endian.h>

#include "apps/bluetooth/lib/common/uuid.h"
#include "apps/bluetooth/lib/gap/advertising_data.h"
#include "apps/bluetooth/lib/gap/discovery_filter.h"

// The internal library components and the generated FIDL bindings are both declared under the
// "bluetooth" namespace. We define an alias here to disambiguate.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {
namespace fidl_helpers {
namespace {

bool PopulateUuids(const ::bluetooth::common::BufferView& data, size_t uuid_size,
                   std::unordered_set<std::string>* to_populate) {
  FTL_DCHECK(to_populate);

  if (data.size() % uuid_size) {
    FTL_LOG(WARNING) << "Malformed service UUIDs list";
    return false;
  }

  size_t uuid_count = data.size() / uuid_size;
  for (size_t i = 0; i < uuid_count; i++) {
    const ::bluetooth::common::BufferView uuid_bytes(data.data() + i * uuid_size, uuid_size);
    ::bluetooth::common::UUID uuid;
    ::bluetooth::common::UUID::FromBytes(uuid_bytes, &uuid);

    to_populate->insert(uuid.ToString());
  }

  return true;
}

bool PopulateServiceData(const ::bluetooth::common::BufferView& data, size_t uuid_size,
                         ::fidl::Map<::fidl::String, ::fidl::Array<uint8_t>>* to_populate) {
  FTL_DCHECK(to_populate);

  if (data.size() < uuid_size) {
    FTL_LOG(WARNING) << "Malformed service UUID in service data";
    return false;
  }

  ::bluetooth::common::UUID uuid;
  ::bluetooth::common::UUID::FromBytes(::bluetooth::common::BufferView(data.data(), uuid_size),
                                       &uuid);
  std::vector<uint8_t> data_vector(data.cbegin() + uuid_size, data.cend() - uuid_size);
  ::fidl::Array<uint8_t> fidl_data;
  fidl_data.Swap(&data_vector);

  to_populate->insert(uuid.ToString(), std::move(fidl_data));

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
        if (data.size() != ::bluetooth::gap::kTxPowerLevelSize) {
          FTL_LOG(WARNING) << "Received malformed Tx Power Level";
          return nullptr;
        }

        auto fidl_tx_power = ::btfidl::Int8::New();
        fidl_tx_power->value = static_cast<int8_t>(data[0]);
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

        if (data.size() != ::bluetooth::gap::kAppearanceSize) {
          FTL_LOG(WARNING) << "Received malformed Appearance";
          return nullptr;
        }

        fidl_device->appearance = static_cast<::btfidl::control::Appearance>(
            le16toh(*reinterpret_cast<const uint16_t*>(data.data())));

        break;
      }
      case ::bluetooth::gap::DataType::kIncomplete16BitServiceUuids:
      case ::bluetooth::gap::DataType::kComplete16BitServiceUuids:
        if (!PopulateUuids(data, ::bluetooth::gap::k16BitUuidElemSize, &uuids)) return nullptr;
        break;
      case ::bluetooth::gap::DataType::kIncomplete32BitServiceUuids:
      case ::bluetooth::gap::DataType::kComplete32BitServiceUuids:
        if (!PopulateUuids(data, ::bluetooth::gap::k32BitUuidElemSize, &uuids)) return nullptr;
        break;
      case ::bluetooth::gap::DataType::kIncomplete128BitServiceUuids:
      case ::bluetooth::gap::DataType::kComplete128BitServiceUuids:
        if (!PopulateUuids(data, ::bluetooth::gap::k128BitUuidElemSize, &uuids)) return nullptr;
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

::btfidl::low_energy::AdvertisingDataPtr NewAdvertisingData(
    const ::bluetooth::common::ByteBuffer& advertising_data) {
  ::bluetooth::gap::AdvertisingDataReader reader(advertising_data);
  if (!reader.is_valid()) return nullptr;

  std::unordered_set<std::string> uuids;
  auto fidl_data = ::btfidl::low_energy::AdvertisingData::New();

  ::bluetooth::gap::DataType type;
  ::bluetooth::common::BufferView data;
  while (reader.GetNextField(&type, &data)) {
    switch (type) {
      case ::bluetooth::gap::DataType::kTxPowerLevel: {
        if (data.size() != ::bluetooth::gap::kTxPowerLevelSize) {
          FTL_LOG(WARNING) << "Received malformed Tx Power Level";
          return nullptr;
        }

        fidl_data->tx_power_level = ::btfidl::Int8::New();
        fidl_data->tx_power_level->value = static_cast<int8_t>(data[0]);
        break;
      }
      case ::bluetooth::gap::DataType::kShortenedLocalName:
        // If a name has been previously set (e.g. because the Complete Local Name was included in
        // the scan response) then break. Otherwise we fall through.
        if (fidl_data->name) break;
      case ::bluetooth::gap::DataType::kCompleteLocalName:
        fidl_data->name = data.ToString();
        break;
      case ::bluetooth::gap::DataType::kIncomplete16BitServiceUuids:
      case ::bluetooth::gap::DataType::kComplete16BitServiceUuids:
        if (!PopulateUuids(data, ::bluetooth::gap::k16BitUuidElemSize, &uuids)) return nullptr;
        break;
      case ::bluetooth::gap::DataType::kIncomplete32BitServiceUuids:
      case ::bluetooth::gap::DataType::kComplete32BitServiceUuids:
        if (!PopulateUuids(data, ::bluetooth::gap::k32BitUuidElemSize, &uuids)) return nullptr;
        break;
      case ::bluetooth::gap::DataType::kIncomplete128BitServiceUuids:
      case ::bluetooth::gap::DataType::kComplete128BitServiceUuids:
        if (!PopulateUuids(data, ::bluetooth::gap::k128BitUuidElemSize, &uuids)) return nullptr;
        break;
      case ::bluetooth::gap::DataType::kServiceData16Bit:
        if (!PopulateServiceData(data, ::bluetooth::gap::k16BitUuidElemSize,
                                 &fidl_data->service_data))
          return nullptr;
        break;
      case ::bluetooth::gap::DataType::kServiceData32Bit:
        if (!PopulateServiceData(data, ::bluetooth::gap::k32BitUuidElemSize,
                                 &fidl_data->service_data))
          return nullptr;
        break;
      case ::bluetooth::gap::DataType::kServiceData128Bit:
        if (!PopulateServiceData(data, ::bluetooth::gap::k128BitUuidElemSize,
                                 &fidl_data->service_data))
          return nullptr;
        break;
      case ::bluetooth::gap::DataType::kManufacturerSpecificData: {
        if (data.size() < ::bluetooth::gap::kManufacturerSpecificDataSizeMin) return nullptr;

        uint16_t id = le16toh(*reinterpret_cast<const uint16_t*>(data.data()));
        std::vector<uint8_t> manuf_data(data.cbegin() + ::bluetooth::gap::kManufacturerIdSize,
                                        data.cend() - ::bluetooth::gap::kManufacturerIdSize);
        fidl::Array<uint8_t> fidl_manuf_data;
        fidl_manuf_data.Swap(&manuf_data);

        fidl_data->manufacturer_specific_data.insert(id, std::move(fidl_manuf_data));
        break;
      }
      default:
        break;
    }
  }

  if (!uuids.empty()) {
    for (const auto& uuid : uuids) {
      fidl_data->service_uuids.push_back(uuid);
    }
  }

  return fidl_data;
}

::btfidl::low_energy::RemoteDevicePtr NewLERemoteDevice(
    const ::bluetooth::gap::RemoteDevice& device) {
  auto fidl_advertising_data = NewAdvertisingData(device.advertising_data());
  if (!fidl_advertising_data) return nullptr;

  auto fidl_device = ::btfidl::low_energy::RemoteDevice::New();
  fidl_device->identifier = device.identifier();
  fidl_device->connectable = device.connectable();
  fidl_device->advertising_data = std::move(fidl_advertising_data);

  return fidl_device;
}

bool IsScanFilterValid(const ::btfidl::low_energy::ScanFilter& fidl_filter) {
  // |service_uuids| is the only field that can potentially contain invalid data, since they are
  // represented as strings.
  if (!fidl_filter.service_uuids) return true;

  for (const auto& uuid_str : fidl_filter.service_uuids) {
    if (!::bluetooth::common::IsStringValidUuid(uuid_str)) return false;
  }

  return true;
}

bool PopulateDiscoveryFilter(const ::btfidl::low_energy::ScanFilter& fidl_filter,
                             ::bluetooth::gap::DiscoveryFilter* out_filter) {
  FTL_DCHECK(out_filter);

  if (fidl_filter.service_uuids) {
    std::vector<::bluetooth::common::UUID> uuids;
    for (const auto& uuid_str : fidl_filter.service_uuids) {
      ::bluetooth::common::UUID uuid;
      if (!::bluetooth::common::StringToUuid(uuid_str, &uuid)) {
        FTL_LOG(WARNING) << "Invalid parameters given to scan filter";
        return false;
      }
      uuids.push_back(uuid);
    }

    if (!uuids.empty()) out_filter->set_service_uuids(uuids);
  }

  if (fidl_filter.connectable) {
    out_filter->set_connectable(fidl_filter.connectable->value);
  }

  if (fidl_filter.manufacturer_identifier) {
    out_filter->set_manufacturer_code(fidl_filter.manufacturer_identifier->value);
  }

  if (fidl_filter.name_substring && !fidl_filter.name_substring.get().empty()) {
    out_filter->set_name_substring(fidl_filter.name_substring.get());
  }

  if (fidl_filter.max_path_loss) {
    out_filter->set_pathloss(fidl_filter.max_path_loss->value);
  }

  return true;
}

}  // namespace fidl_helpers
}  // namespace bluetooth_service
