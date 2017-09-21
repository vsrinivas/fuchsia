// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include "lib/fxl/logging.h"

namespace bluetooth {
namespace hci {

std::string HCIVersionToString(hci::HCIVersion version) {
  switch (version) {
    case hci::HCIVersion::k1_0b:
      return "1.0b";
    case hci::HCIVersion::k1_1:
      return "1.1";
    case hci::HCIVersion::k1_2:
      return "1.2";
    case hci::HCIVersion::k2_0_EDR:
      return "2.0 + EDR";
    case hci::HCIVersion::k2_1_EDR:
      return "2.1 + EDR";
    case hci::HCIVersion::k3_0_HS:
      return "3.0 + HS";
    case hci::HCIVersion::k4_0:
      return "4.0";
    case hci::HCIVersion::k4_1:
      return "4.1";
    case hci::HCIVersion::k4_2:
      return "4.2";
    case hci::HCIVersion::k5_0:
      return "5.0";
    default:
      break;
  }
  return "(unknown)";
}

bool DeviceAddressFromAdvReport(const hci::LEAdvertisingReportData& report,
                                common::DeviceAddress* out_address) {
  FXL_DCHECK(out_address);

  common::DeviceAddress::Type type;
  switch (report.address_type) {
    case hci::LEAddressType::kPublic:
    case hci::LEAddressType::kPublicIdentity:
      type = common::DeviceAddress::Type::kLEPublic;
      break;
    case hci::LEAddressType::kRandom:
    case hci::LEAddressType::kRandomIdentity:
      type = common::DeviceAddress::Type::kLERandom;
      break;
    default:
      FXL_LOG(WARNING) << "gap: LegacyLowEnergyScanManager: Invalid address "
                          "type in advertising report: "
                       << static_cast<int>(report.address_type);
      return false;
  }

  *out_address = common::DeviceAddress(type, report.address);
  return true;
}

common::DeviceAddress::Type AddressTypeFromHCI(LEAddressType type) {
  common::DeviceAddress::Type result;
  switch (type) {
    case LEAddressType::kPublic:
    case LEAddressType::kPublicIdentity:
      result = common::DeviceAddress::Type::kLEPublic;
      break;
    case LEAddressType::kRandom:
    case LEAddressType::kRandomIdentity:
    case LEAddressType::kRandomUnresolved:
      result = common::DeviceAddress::Type::kLERandom;
      break;
    case LEAddressType::kAnonymous:
      result = common::DeviceAddress::Type::kLEAnonymous;
      break;
  }
  return result;
}

common::DeviceAddress::Type AddressTypeFromHCI(LEPeerAddressType type) {
  common::DeviceAddress::Type result;
  switch (type) {
    case LEPeerAddressType::kPublic:
      result = common::DeviceAddress::Type::kLEPublic;
      break;
    case LEPeerAddressType::kRandom:
      result = common::DeviceAddress::Type::kLERandom;
      break;
    case LEPeerAddressType::kAnonymous:
      result = common::DeviceAddress::Type::kLEAnonymous;
      break;
  }
  return result;
}

}  // namespace hci
}  // namespace bluetooth
