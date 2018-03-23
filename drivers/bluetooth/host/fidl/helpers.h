// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/gap/adapter.h"
#include "garnet/drivers/bluetooth/lib/gap/remote_device.h"
#include <fuchsia/cpp/bluetooth.h>
#include <fuchsia/cpp/bluetooth_control.h>
#include <fuchsia/cpp/bluetooth_gatt.h>
#include <fuchsia/cpp/bluetooth_low_energy.h>
#include "lib/fxl/type_converter.h"
#include "lib/fidl/cpp/vector.h"

// Helpers for implementing the Bluetooth FIDL interfaces.

namespace btlib {
namespace gap {

class DiscoveryFilter;

}  // namespace gap
}  // namespace btlib

namespace bthost {
namespace fidl_helpers {

::bluetooth::Status NewErrorStatus(::bluetooth::ErrorCode error_code,
                                      const std::string& description);
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
