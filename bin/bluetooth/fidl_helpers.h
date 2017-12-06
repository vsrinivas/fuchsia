// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include "lib/bluetooth/fidl/common.fidl.h"
#include "lib/bluetooth/fidl/control.fidl.h"
#include "lib/bluetooth/fidl/gatt.fidl.h"
#include "lib/bluetooth/fidl/low_energy.fidl.h"

// Helpers for implementing the Bluetooth FIDL interfaces.

namespace btlib {
namespace gap {

class DiscoveryFilter;

}  // namespace gap
}  // namespace btlib

namespace bluetooth_service {
namespace fidl_helpers {

::bluetooth::StatusPtr NewErrorStatus(::bluetooth::ErrorCode error_code,
                                      const std::string& description);
::bluetooth::control::AdapterInfoPtr NewAdapterInfo(
    const ::btlib::gap::Adapter& adapter);
::bluetooth::control::RemoteDevicePtr NewRemoteDevice(
    const ::btlib::gap::RemoteDevice& device);

::bluetooth::low_energy::AdvertisingDataPtr NewAdvertisingData(
    const ::btlib::common::ByteBuffer& advertising_data);
::bluetooth::low_energy::RemoteDevicePtr NewLERemoteDevice(
    const ::btlib::gap::RemoteDevice& device);

// Validates the contents of a ScanFilter.
bool IsScanFilterValid(const ::bluetooth::low_energy::ScanFilter& fidl_filter);

// Populates a library DiscoveryFilter based on a FIDL ScanFilter. Returns false
// if |fidl_filter| contains any malformed data and leaves |out_filter|
// unmodified.
bool PopulateDiscoveryFilter(
    const ::bluetooth::low_energy::ScanFilter& fidl_filter,
    ::btlib::gap::DiscoveryFilter* out_filter);

}  // namespace fidl_helpers
}  // namespace bluetooth_service

// fidl::TypeConverter specializations for common::ByteBuffer and friends.
namespace fidl {

template <>
struct TypeConverter<Array<uint8_t>, ::btlib::common::ByteBuffer> {
  static Array<uint8_t> Convert(const ::btlib::common::ByteBuffer& from);
};

}  // namespace fidl
