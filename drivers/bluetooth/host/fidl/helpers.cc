// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helpers.h"

#include <unordered_set>

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/common/log.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/gap/discovery_filter.h"

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
      ZX_PANIC("invalid technology type: %u", static_cast<unsigned int>(type));
      break;
  }

  // This should never execute.
  return ctrl::TechnologyType::DUAL_MODE;
}

btlib::common::UInt128 KeyDataFromFidl(const ctrl::Key& key) {
  btlib::common::UInt128 result;
  static_assert(sizeof(key.value) == result.size(),
                "keys must have the same size");
  std::copy(key.value.begin(), key.value.end(), result.begin());
  return result;
}

::fidl::Array<uint8_t, 16> KeyDataToFidl(const btlib::common::UInt128& key) {
  ::fidl::Array<uint8_t, 16> result;
  static_assert(sizeof(key) == result.size(), "keys must have the same size");
  std::copy(key.begin(), key.end(), result.begin());
  return result;
}

btlib::sm::SecurityProperties SecurityPropsFromFidl(
    const ctrl::SecurityProperties& sec_prop) {
  auto level = btlib::sm::SecurityLevel::kEncrypted;
  if (sec_prop.authenticated) {
    level = btlib::sm::SecurityLevel::kAuthenticated;
  }
  return btlib::sm::SecurityProperties(level, sec_prop.encryption_key_size,
                                       sec_prop.secure_connections);
}

ctrl::SecurityProperties SecurityPropsToFidl(
    const btlib::sm::SecurityProperties& sec_prop) {
  ctrl::SecurityProperties result;
  result.authenticated = sec_prop.authenticated();
  result.secure_connections = sec_prop.secure_connections();
  result.encryption_key_size = sec_prop.enc_key_size();
  return result;
}

btlib::common::DeviceAddress::Type BondingAddrTypeFromFidl(
    const fuchsia::bluetooth::control::AddressType& type) {
  switch (type) {
    case ctrl::AddressType::LE_RANDOM:
      return btlib::common::DeviceAddress::Type::kLERandom;
    case ctrl::AddressType::LE_PUBLIC:
      return btlib::common::DeviceAddress::Type::kLEPublic;
    case ctrl::AddressType::BREDR:
      return btlib::common::DeviceAddress::Type::kBREDR;
    default:
      ZX_PANIC("invalid address type: %u", static_cast<unsigned int>(type));
      break;
  }
  return btlib::common::DeviceAddress::Type::kBREDR;
}

fuchsia::bluetooth::control::AddressType BondingAddrTypeToFidl(
    btlib::common::DeviceAddress::Type type) {
  switch (type) {
    case btlib::common::DeviceAddress::Type::kLERandom:
      return ctrl::AddressType::LE_RANDOM;
    case btlib::common::DeviceAddress::Type::kLEPublic:
      return ctrl::AddressType::LE_PUBLIC;
    case btlib::common::DeviceAddress::Type::kBREDR:
      return ctrl::AddressType::BREDR;
    default:
      // Anonymous is not a valid address type to use for bonding, so we treat
      // that as a programming error.
      ZX_PANIC("invalid address type for bonding: %u",
               static_cast<unsigned int>(type));
      break;
  }
  return ctrl::AddressType::BREDR;
}

btlib::sm::LTK LtkFromFidl(const ctrl::LTK& ltk) {
  return btlib::sm::LTK(
      SecurityPropsFromFidl(ltk.key.security_properties),
      btlib::hci::LinkKey(KeyDataFromFidl(ltk.key), ltk.rand, ltk.ediv));
}

ctrl::LTK LtkToFidl(const btlib::sm::LTK& ltk) {
  ctrl::LTK result;
  result.key.security_properties = SecurityPropsToFidl(ltk.security());
  result.key.value = KeyDataToFidl(ltk.key().value());

  // TODO(armansito): Remove this field since its already captured in security
  // properties.
  result.key_size = ltk.security().enc_key_size();
  result.rand = ltk.key().rand();
  result.ediv = ltk.key().ediv();
  return result;
}

btlib::sm::Key KeyFromFidl(const ctrl::Key& key) {
  return btlib::sm::Key(SecurityPropsFromFidl(key.security_properties),
                        KeyDataFromFidl(key));
}

ctrl::Key KeyToFidl(const btlib::sm::Key& key) {
  ctrl::Key result;
  result.security_properties = SecurityPropsToFidl(key.security());
  result.value = KeyDataToFidl(key.value());
  return result;
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

btlib::sm::IOCapability IoCapabilityFromFidl(
    ctrl::InputCapabilityType input, ctrl::OutputCapabilityType output) {
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

btlib::sm::PairingData PairingDataFromFidl(
    const fuchsia::bluetooth::control::LEData& data) {
  btlib::sm::PairingData result;
  result.identity_address = btlib::common::DeviceAddress(
      BondingAddrTypeFromFidl(data.address_type), data.address);
  if (data.ltk) {
    result.ltk = LtkFromFidl(*data.ltk);
  }
  if (data.irk) {
    result.irk = KeyFromFidl(*data.irk);
  }
  if (data.csrk) {
    result.csrk = KeyFromFidl(*data.csrk);
  }
  return result;
}

ctrl::AdapterInfo NewAdapterInfo(const ::btlib::gap::Adapter& adapter) {
  ctrl::AdapterInfo adapter_info;
  adapter_info.identifier = adapter.identifier();
  adapter_info.technology = TechnologyTypeToFidl(adapter.state().type());
  adapter_info.address = adapter.state().controller_address().ToString();

  adapter_info.state = ctrl::AdapterState::New();
  adapter_info.state->local_name = adapter.state().local_name();
  adapter_info.state->discoverable = Bool::New();
  adapter_info.state->discoverable->value = false;
  adapter_info.state->discovering = Bool::New();
  adapter_info.state->discovering->value = adapter.IsDiscovering();

  // TODO(armansito): Populate |local_service_uuids| as well.

  return adapter_info;
}

ctrl::RemoteDevice NewRemoteDevice(const ::btlib::gap::RemoteDevice& device) {
  ctrl::RemoteDevice fidl_device;
  fidl_device.identifier = device.identifier();
  fidl_device.address = device.address().value().ToString();
  fidl_device.technology = TechnologyTypeToFidl(device.technology());
  fidl_device.connected = device.connected();
  fidl_device.bonded = device.bonded();

  // Set default value for device appearance.
  fidl_device.appearance = ctrl::Appearance::UNKNOWN;

  // |service_uuids| is not a nullable field, so we need to assign something
  // to it.
  fidl_device.service_uuids.resize(0);

  if (device.rssi() != ::btlib::hci::kRSSIInvalid) {
    fidl_device.rssi = Int8::New();
    fidl_device.rssi->value = device.rssi();
  }

  if (device.name()) {
    fidl_device.name = *device.name();
  }

  if (device.le()) {
    ::btlib::gap::AdvertisingData adv_data;

    if (!::btlib::gap::AdvertisingData::FromBytes(
            device.le()->advertising_data(), &adv_data)) {
      return fidl_device;
    }

    for (const auto& uuid : adv_data.service_uuids()) {
      fidl_device.service_uuids.push_back(uuid.ToString());
    }
    if (adv_data.appearance()) {
      fidl_device.appearance =
          static_cast<ctrl::Appearance>(le16toh(*adv_data.appearance()));
    }
    if (adv_data.tx_power()) {
      auto fidl_tx_power = Int8::New();
      fidl_tx_power->value = *adv_data.tx_power();
      fidl_device.tx_power = std::move(fidl_tx_power);
    }
  }

  return fidl_device;
}

ctrl::RemoteDevicePtr NewRemoteDevicePtr(
    const ::btlib::gap::RemoteDevice& device) {
  auto fidl_device = ctrl::RemoteDevice::New();
  *fidl_device = NewRemoteDevice(device);
  return fidl_device;
}

ctrl::BondingData NewBondingData(const ::btlib::gap::Adapter& adapter,
                                 const ::btlib::gap::RemoteDevice& device) {
  ctrl::BondingData out_data;
  out_data.identifier = device.identifier();
  out_data.local_address = adapter.state().controller_address().ToString();

  if (device.name()) {
    out_data.name = *device.name();
  }

  // Store LE data.
  if (device.le() && device.le()->bond_data()) {
    out_data.le = ctrl::LEData::New();

    const auto& le_data = *device.le()->bond_data();
    const auto& identity =
        le_data.identity_address ? *le_data.identity_address : device.address();
    out_data.le->address = identity.value().ToString();
    out_data.le->address_type = BondingAddrTypeToFidl(identity.type());

    // TODO(armansito): Populate the preferred connection parameters here.

    // TODO(armansito): Populate with discovered GATT services. We initialize
    // this as empty as |services| is not nullable.
    out_data.le->services.resize(0);

    if (le_data.ltk) {
      out_data.le->ltk = ctrl::LTK::New();
      *out_data.le->ltk = LtkToFidl(*le_data.ltk);
    }
    if (le_data.irk) {
      out_data.le->irk = ctrl::Key::New();
      *out_data.le->irk = KeyToFidl(*le_data.irk);
    }
    if (le_data.csrk) {
      out_data.le->csrk = ctrl::Key::New();
      *out_data.le->csrk = KeyToFidl(*le_data.csrk);
    }
  }

  // TODO(armansito): Store BR/EDR data.
  return out_data;
}

ble::RemoteDevicePtr NewLERemoteDevice(
    const ::btlib::gap::RemoteDevice& device) {
  ::btlib::gap::AdvertisingData ad;
  if (!device.le()) {
    return nullptr;
  }

  const auto& le = *device.le();
  auto fidl_device = ble::RemoteDevice::New();
  fidl_device->identifier = device.identifier();
  fidl_device->connectable = device.connectable();

  // Initialize advertising data only if its non-empty.
  if (le.advertising_data().size() != 0u) {
    ::btlib::gap::AdvertisingData ad;
    if (!::btlib::gap::AdvertisingData::FromBytes(le.advertising_data(), &ad)) {
      return nullptr;
    }
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
  ZX_DEBUG_ASSERT(out_filter);

  if (fidl_filter.service_uuids) {
    std::vector<::btlib::common::UUID> uuids;
    for (const auto& uuid_str : *fidl_filter.service_uuids) {
      ::btlib::common::UUID uuid;
      if (!::btlib::common::StringToUuid(uuid_str, &uuid)) {
        bt_log(TRACE, "bt-host", "invalid parameters given to scan filter");
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
fxl::TypeConverter<fidl::VectorPtr<uint8_t>, ::btlib::common::ByteBuffer>::
    Convert(const ::btlib::common::ByteBuffer& from) {
  auto to = fidl::VectorPtr<uint8_t>::New(from.size());
  ::btlib::common::MutableBufferView view(to->data(), to->size());
  view.Write(from);
  return to;
}
