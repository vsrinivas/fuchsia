// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/bluetooth/control/cpp/fidl.h>
#include <fuchsia/bluetooth/cpp/fidl.h>
#include <fuchsia/bluetooth/gatt/cpp/fidl.h>
#include <fuchsia/bluetooth/le/cpp/fidl.h>

#include "lib/fidl/cpp/vector.h"
#include "lib/fxl/type_converter.h"

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/status.h"
#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"

// Helpers for implementing the Bluetooth FIDL interfaces.

namespace btlib {
namespace gap {

class DiscoveryFilter;

}  // namespace gap
}  // namespace btlib

namespace bthost {
namespace fidl_helpers {

// Functions for generating a FIDL bluetooth::common::Status

fuchsia::bluetooth::ErrorCode HostErrorToFidl(
    ::btlib::common::HostError host_error);

fuchsia::bluetooth::Status NewFidlError(
    fuchsia::bluetooth::ErrorCode error_code, std::string description);

template <typename ProtocolErrorCode>
fuchsia::bluetooth::Status StatusToFidl(
    const ::btlib::common::Status<ProtocolErrorCode>& status,
    std::string msg = "") {
  fuchsia::bluetooth::Status fidl_status;

  if (status.is_success())
    return fidl_status;

  auto error = fuchsia::bluetooth::Error::New();
  error->error_code = HostErrorToFidl(status.error());
  error->description = msg.empty() ? status.ToString() : std::move(msg);
  if (status.is_protocol_error()) {
    error->protocol_error_code = static_cast<uint32_t>(status.protocol_error());
  }

  fidl_status.error = std::move(error);
  return fidl_status;
}

// Functions that convert FIDL types to library objects
btlib::sm::SecurityProperties NewSecurityLevel(
    const fuchsia::bluetooth::control::SecurityProperties& sec_prop);
btlib::common::DeviceAddress::Type NewAddrType(
    const fuchsia::bluetooth::control::AddressType& type);
btlib::sm::IOCapability NewIoCapability(
    const fuchsia::bluetooth::control::InputCapabilityType,
    const fuchsia::bluetooth::control::OutputCapabilityType);

// Functions to convert host library objects into FIDL types.

fuchsia::bluetooth::control::AdapterInfo NewAdapterInfo(
    const ::btlib::gap::Adapter& adapter);
fuchsia::bluetooth::control::RemoteDevicePtr NewRemoteDevice(
    const ::btlib::gap::RemoteDevice& device);

fuchsia::bluetooth::le::AdvertisingDataPtr NewAdvertisingData(
    const ::btlib::common::ByteBuffer& advertising_data);
fuchsia::bluetooth::le::RemoteDevicePtr NewLERemoteDevice(
    const ::btlib::gap::RemoteDevice& device);

// Validates the contents of a ScanFilter.
bool IsScanFilterValid(const fuchsia::bluetooth::le::ScanFilter& fidl_filter);

// Populates a library DiscoveryFilter based on a FIDL ScanFilter. Returns false
// if |fidl_filter| contains any malformed data and leaves |out_filter|
// unmodified.
bool PopulateDiscoveryFilter(
    const fuchsia::bluetooth::le::ScanFilter& fidl_filter,
    ::btlib::gap::DiscoveryFilter* out_filter);

}  // namespace fidl_helpers
}  // namespace bthost

// fxl::TypeConverter specializations for common::ByteBuffer and friends.
template <>
struct fxl::TypeConverter<fidl::VectorPtr<uint8_t>, ::btlib::common::ByteBuffer> {
  static fidl::VectorPtr<uint8_t> Convert(const ::btlib::common::ByteBuffer& from);
};
