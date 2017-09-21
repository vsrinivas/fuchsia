// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"

namespace bluetooth {
namespace hci {

// Returns a user-friendly string representation of |version|.
std::string HCIVersionToString(hci::HCIVersion version);

// Constructs a common::DeviceAddress structure from the contents of the given
// advertising report. Returns false if the report contain an invalid value.
bool DeviceAddressFromAdvReport(const hci::LEAdvertisingReportData& report,
                                common::DeviceAddress* out_address);

common::DeviceAddress::Type AddressTypeFromHCI(LEAddressType type);
common::DeviceAddress::Type AddressTypeFromHCI(LEPeerAddressType type);

}  // namespace hci
}  // namespace bluetooth
