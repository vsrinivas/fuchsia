// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helpers.h"

#include <unordered_set>

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/gap/discovery_filter.h"

using btlib::sm::SecurityLevel;
using fuchsia::bluetooth::Bool;
using fuchsia::bluetooth::Error;
using fuchsia::bluetooth::ErrorCode;
using fuchsia::bluetooth::Int8;
using fuchsia::bluetooth::Status;

namespace ctrl = fuchsia::bluetooth::control;
namespace ble = fuchsia::bluetooth::le;

namespace bthost {
namespace fidl_helpers {
namespace {

ctrl::TechnologyType TechnologyTypeToFidl(::btlib::gap::TechnologyType type) {
  switch (type) {
    case ::btlib::gap::TechnologyType::kLowEnergy:
      return ctrl::TechnologyType::LOW_ENERGY;
    case ::btlib::gap::TechnologyType::kClassic:
      return ctrl::TechnologyType::CLASSIC;
    case ::btlib::gap::TechnologyType::kDualMode:
      return ctrl::TechnologyType::DUAL_MODE;
    default:
      FXL_NOTREACHED();
      break;
  }

  // This should never execute.
  return ctrl::TechnologyType::DUAL_MODE;
}

}  // namespace

ErrorCode HostErrorToFidl(::btlib::common::HostError host_error) {
  switch (host_error) {
    case ::btlib::common::HostError::kFailed:
      return ErrorCode::FAILED;
    case ::btlib::common::HostError::kTimedOut:
      return ErrorCode::TIMED_OUT;
    case ::btlib::common::HostError::kInvalidParameters:
      return ErrorCode::INVALID_ARGUMENTS;
    case ::btlib::common::HostError::kCanceled:
      return ErrorCode::CANCELED;
    case ::btlib::common::HostError::kInProgress:
      return ErrorCode::IN_PROGRESS;
    case ::btlib::common::HostError::kNotSupported:
      return ErrorCode::NOT_SUPPORTED;
    case ::btlib::common::HostError::kNotFound:
      return ErrorCode::NOT_FOUND;
    case ::btlib::common::HostError::kProtocolError:
      return ErrorCode::PROTOCOL_ERROR;
    default:
      break;
  }

  return ErrorCode::FAILED;
}

Status NewFidlError(ErrorCode error_code, std::string description) {
  Status status;
  status.error = Error::New();
  status.error->error_code = error_code;
  status.error->description = description;
  return status;
}

btlib::common::DeviceAddress::Type NewAddrType(
    const fuchsia::bluetooth::control::AddressType& type) {
  switch (type) {
    case ctrl::AddressType::LE_RANDOM:
      return btlib::common::DeviceAddress::Type::kLERandom;
    case ctrl::AddressType::LE_PUBLIC:
      return btlib::common::DeviceAddress::Type::kLEPublic;
    case ctrl::AddressType::BREDR:
      return btlib::common::DeviceAddress::Type::kBREDR;
    default:
      FXL_NOTREACHED();
      break;
  }
  return btlib::common::DeviceAddress::Type::kBREDR;
}

btlib::sm::SecurityProperties NewSecurityLevel(
    const fuchsia::bluetooth::control::SecurityProperties& sec_prop) {
  auto level = btlib::sm::SecurityLevel::kEncrypted;
  if (sec_prop.authenticated) {
    level = btlib::sm::SecurityLevel::kAuthenticated;
  }

  return btlib::sm::SecurityProperties(level, sec_prop.encryption_key_size,
                                       sec_prop.secure_connections);
}

btlib::sm::IOCapability NewIoCapability(ctrl::InputCapabilityType input,
                                        ctrl::OutputCapabilityType output) {
  if (input == ctrl::InputCapabilityType::NONE &&
      output == ctrl::OutputCapabilityType::NONE) {
    return btlib::sm::IOCapability::kNoInputNoOutput;
  } else if (input == ctrl::InputCapabilityType::KEYBOARD &&
             output == ctrl::OutputCapabilityType::DISPLAY) {
    return btlib::sm::IOCapability::kKeyboardDisplay;
  } else if (input == ctrl::InputCapabilityType::KEYBOARD &&
             output == ctrl::OutputCapabilityType::NONE) {
    return btlib::sm::IOCapability::kKeyboardOnly;
  } else if (input == ctrl::InputCapabilityType::NONE &&
             output == ctrl::OutputCapabilityType::DISPLAY) {
    return btlib::sm::IOCapability::kDisplayOnly;
  } else if (input == ctrl::InputCapabilityType::CONFIRMATION &&
             output == ctrl::OutputCapabilityType::DISPLAY) {
    return btlib::sm::IOCapability::kDisplayYesNo;
  }
  return btlib::sm::IOCapability::kNoInputNoOutput;
}

ctrl::AdapterInfo NewAdapterInfo(const ::btlib::gap::Adapter& adapter) {
  ctrl::AdapterInfo adapter_info;
  adapter_info.state = ctrl::AdapterState::New();

  adapter_info.state->local_name = adapter.state().local_name();

  adapter_info.state->discoverable = Bool::New();
  adapter_info.state->discoverable->value = false;
  adapter_info.state->discovering = Bool::New();
  adapter_info.state->discovering->value = adapter.IsDiscovering();

  adapter_info.identifier = adapter.identifier();
  adapter_info.address = adapter.state().controller_address().ToString();

  adapter_info.technology = TechnologyTypeToFidl(adapter.state().type());

  return adapter_info;
}

ctrl::RemoteDevicePtr NewRemoteDevice(
    const ::btlib::gap::RemoteDevice& device) {
  auto fidl_device = ctrl::RemoteDevice::New();
  fidl_device->identifier = device.identifier();
  fidl_device->address = device.address().value().ToString();
  fidl_device->technology = TechnologyTypeToFidl(device.technology());

  // TODO(armansito): Report correct values once we support these.
  fidl_device->connected = false;
  fidl_device->bonded = false;

  // Set default value for device appearance.
  fidl_device->appearance = ctrl::Appearance::UNKNOWN;

  if (device.rssi() != ::btlib::hci::kRSSIInvalid) {
    fidl_device->rssi = Int8::New();
    fidl_device->rssi->value = device.rssi();
  }

  ::btlib::gap::AdvertisingData adv_data;
  if (!::btlib::gap::AdvertisingData::FromBytes(device.advertising_data(),
                                                &adv_data)) {
    fidl_device->service_uuids.resize(0);
    return fidl_device;
  }

  const auto& uuids = adv_data.service_uuids();

  // |service_uuids| is not a nullable field, so we need to assign something to
  // it.
  if (uuids.empty()) {
    fidl_device->service_uuids.resize(0);
  } else {
    for (const auto& uuid : uuids) {
      fidl_device->service_uuids.push_back(uuid.ToString());
    }
  }

  if (adv_data.local_name())
    fidl_device->name = *adv_data.local_name();
  if (adv_data.appearance()) {
    fidl_device->appearance =
        static_cast<ctrl::Appearance>(le16toh(*adv_data.appearance()));
  }
  if (adv_data.tx_power()) {
    auto fidl_tx_power = Int8::New();
    fidl_tx_power->value = *adv_data.tx_power();
    fidl_device->tx_power = std::move(fidl_tx_power);
  }

  return fidl_device;
}

ble::RemoteDevicePtr NewLERemoteDevice(
    const ::btlib::gap::RemoteDevice& device) {
  ::btlib::gap::AdvertisingData ad;
  auto fidl_device = ble::RemoteDevice::New();
  fidl_device->identifier = device.identifier();
  fidl_device->connectable = device.connectable();

  // Initialize advertising data only if its non-empty.
  if (device.advertising_data().size() != 0u) {
    ::btlib::gap::AdvertisingData ad;
    if (!::btlib::gap::AdvertisingData::FromBytes(device.advertising_data(),
                                                  &ad))
      return nullptr;

    fidl_device->advertising_data = ad.AsLEAdvertisingData();
  }

  if (device.rssi() != ::btlib::hci::kRSSIInvalid) {
    fidl_device->rssi = Int8::New();
    fidl_device->rssi->value = device.rssi();
  }

  return fidl_device;
}

bool IsScanFilterValid(const ble::ScanFilter& fidl_filter) {
  // |service_uuids| is the only field that can potentially contain invalid
  // data, since they are represented as strings.
  if (!fidl_filter.service_uuids)
    return true;

  for (const auto& uuid_str : *fidl_filter.service_uuids) {
    if (!::btlib::common::IsStringValidUuid(uuid_str))
      return false;
  }

  return true;
}

bool PopulateDiscoveryFilter(const ble::ScanFilter& fidl_filter,
                             ::btlib::gap::DiscoveryFilter* out_filter) {
  FXL_DCHECK(out_filter);

  if (fidl_filter.service_uuids) {
    std::vector<::btlib::common::UUID> uuids;
    for (const auto& uuid_str : *fidl_filter.service_uuids) {
      ::btlib::common::UUID uuid;
      if (!::btlib::common::StringToUuid(uuid_str, &uuid)) {
        FXL_VLOG(1) << "Invalid parameters given to scan filter";
        return false;
      }
      uuids.push_back(uuid);
    }

    if (!uuids.empty())
      out_filter->set_service_uuids(uuids);
  }

  if (fidl_filter.connectable) {
    out_filter->set_connectable(fidl_filter.connectable->value);
  }

  if (fidl_filter.manufacturer_identifier) {
    out_filter->set_manufacturer_code(
        fidl_filter.manufacturer_identifier->value);
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
}  // namespace bthost

// static
fidl::VectorPtr<uint8_t>
fxl::TypeConverter<fidl::VectorPtr<uint8_t>, ::btlib::common::ByteBuffer>::Convert(
    const ::btlib::common::ByteBuffer& from) {
  auto to = fidl::VectorPtr<uint8_t>::New(from.size());
  ::btlib::common::MutableBufferView view(to->data(), to->size());
  view.Write(from);
  return to;
}
