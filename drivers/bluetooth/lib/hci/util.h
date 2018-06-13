// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/drivers/bluetooth/lib/common/byte_buffer.h"
#include "garnet/drivers/bluetooth/lib/common/device_address.h"
#include "garnet/drivers/bluetooth/lib/hci/hci.h"

namespace btlib {
namespace hci {

// Returns a user-friendly string representation of |version|.
std::string HCIVersionToString(hci::HCIVersion version);

// Returns a user-friendly string representation of |status|.
std::string StatusCodeToString(hci::StatusCode code);

// Constructs a common::DeviceAddress structure from the contents of the given
// advertising report. Returns false if the report contain an invalid value.
bool DeviceAddressFromAdvReport(const hci::LEAdvertisingReportData& report,
                                common::DeviceAddress* out_address);

// Convert HCI LE device address type to our stack type.
common::DeviceAddress::Type AddressTypeFromHCI(LEAddressType type);
common::DeviceAddress::Type AddressTypeFromHCI(LEPeerAddressType type);

// Convert our stack LE address type to HCI type. |type| cannot be kBREDR.
LEAddressType AddressTypeToHCI(common::DeviceAddress::Type type);

}  // namespace hci
}  // namespace btlib
