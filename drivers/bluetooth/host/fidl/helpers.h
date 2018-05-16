// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <bluetooth/cpp/fidl.h>
#include <bluetooth_control/cpp/fidl.h>
#include <bluetooth_gatt/cpp/fidl.h>
#include <bluetooth_low_energy/cpp/fidl.h>

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

// Functions for generating a FIDL bluetooth::Status

::bluetooth::ErrorCode HostErrorToFidl(::btlib::common::HostError host_error);

::bluetooth::Status NewFidlError(::bluetooth::ErrorCode error_code,
                                 std::string description);

template <typename ProtocolErrorCode>
::bluetooth::Status StatusToFidl(
    const ::btlib::common::Status<ProtocolErrorCode>& status,
    std::string msg = "") {
  ::bluetooth::Status fidl_status;

  if (status.is_success())
    return fidl_status;

  auto error = ::bluetooth::Error::New();
  error->error_code = HostErrorToFidl(status.error());
  error->description = msg.empty() ? status.ToString() : std::move(msg);
  if (status.is_protocol_error()) {
    error->protocol_error_code = static_cast<uint32_t>(status.protocol_error());
  }

  fidl_status.error = std::move(error);
  return fidl_status;
}

// Functions to convert host library objects into FIDL types.

::bluetooth_control::AdapterInfo NewAdapterInfo(
    const ::btlib::gap::Adapter& adapter);
::bluetooth_control::RemoteDevicePtr NewRemoteDevice(
    const ::btlib::gap::RemoteDevice& device);

::bluetooth_low_energy::AdvertisingDataPtr NewAdvertisingData(
    const ::btlib::common::ByteBuffer& advertising_data);
::bluetooth_low_energy::RemoteDevicePtr NewLERemoteDevice(
    const ::btlib::gap::RemoteDevice& device);

// Validates the contents of a ScanFilter.
bool IsScanFilterValid(const ::bluetooth_low_energy::ScanFilter& fidl_filter);

// Populates a library DiscoveryFilter based on a FIDL ScanFilter. Returns false
// if |fidl_filter| contains any malformed data and leaves |out_filter|
// unmodified.
bool PopulateDiscoveryFilter(
    const ::bluetooth_low_energy::ScanFilter& fidl_filter,
    ::btlib::gap::DiscoveryFilter* out_filter);

}  // namespace fidl_helpers
}  // namespace bthost

// fxl::TypeConverter specializations for common::ByteBuffer and friends.
template <>
struct fxl::TypeConverter<fidl::VectorPtr<uint8_t>, ::btlib::common::ByteBuffer> {
  static fidl::VectorPtr<uint8_t> Convert(const ::btlib::common::ByteBuffer& from);
};
