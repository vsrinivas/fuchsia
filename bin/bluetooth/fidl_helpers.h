// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/bluetooth/lib/gap/adapter.h"
#include "apps/bluetooth/lib/gap/remote_device.h"
#include "apps/bluetooth/service/interfaces/common.fidl.h"
#include "apps/bluetooth/service/interfaces/control.fidl.h"
#include "apps/bluetooth/service/interfaces/low_energy.fidl.h"

// Helpers for implementing the Bluetooth FIDL interfaces.

namespace bluetooth {
namespace gap {

class DiscoveryFilter;

}  // namespace gap
}  // namespace bluetooth

namespace bluetooth_service {
namespace fidl_helpers {

::bluetooth::StatusPtr NewErrorStatus(::bluetooth::ErrorCode error_code,
                                      const std::string& description);
::bluetooth::control::AdapterInfoPtr NewAdapterInfo(const ::bluetooth::gap::Adapter& adapter);
::bluetooth::control::RemoteDevicePtr NewRemoteDevice(const ::bluetooth::gap::RemoteDevice& device);

::bluetooth::low_energy::AdvertisingDataPtr NewAdvertisingData(
    const ::bluetooth::common::ByteBuffer& advertising_data);
::bluetooth::low_energy::RemoteDevicePtr NewLERemoteDevice(
    const ::bluetooth::gap::RemoteDevice& device);

// Validates the contents of a ScanFilter.
bool IsScanFilterValid(const ::bluetooth::low_energy::ScanFilter& fidl_filter);

// Populates a library DiscoveryFilter based on a FIDL ScanFilter. Returns false if |fidl_filter|
// contains any malformed data and leaves |out_filter| unmodified.
bool PopulateDiscoveryFilter(const ::bluetooth::low_energy::ScanFilter& fidl_filter,
                             ::bluetooth::gap::DiscoveryFilter* out_filter);

}  // namespace fidl_helpers
}  // namespace bluetooth_service
