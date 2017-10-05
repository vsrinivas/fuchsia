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

std::string StatusToString(const hci::Status& status) {
  switch (status) {
    case hci::kSuccess: return "Success";
    case hci::kUnknownCommand: return "Unknown command";
    case hci::kUnknownConnectionId: return "Unknown connection ID";
    case hci::kHardwareFailure: return "Hardware failure";
    case hci::kPageTimeout: return "Page timeout";
    case hci::kAuthenticationFailure: return "Authentication failure";
    case hci::kPinOrKeyMissing: return "Pin or key missing";
    case hci::kMemoryCapacityExceeded: return "Memory capacity exceeded";
    case hci::kConnectionTimeout: return "Connection timeout";
    case hci::kConnectionLimitExceeded: return "Connection limit exceeded";
    case hci::kSynchronousConnectionLimitExceeded: return "Synchronous connection limit exceeded";
    case hci::kConnectionAlreadyExists: return "Connection already exists";
    case hci::kCommandDisallowed: return "Command disallowed";
    case hci::kConnectionRejectedLimitedResources: return "Connection rejected: limited resources";
    case hci::kConnectionRejectedSecurity: return "Connection rejected: security";
    case hci::kConnectionRejectedBadBdAddr: return "Connection rejected: bad BD_ADDR";
    case hci::kConnectionAcceptTimeoutExceeded: return "Connection accept timeout exceeded";
    case hci::kUnsupportedFeatureOrParameter: return "Unsupported feature or parameter";
    case hci::kInvalidHCICommandParameters: return "Invalid HCI command parameters";
    case hci::kRemoteUserTerminatedConnection: return "Remote user terminated connection";
    case hci::kRemoteDeviceTerminatedConnectionLowResources: return "Remote device terminated connection: low resources";
    case hci::kRemoteDeviceTerminatedConnectionPowerOff: return "Remote device rerminated connection: power off";
    case hci::kConnectionTerminatedByLocalHost: return "Connection terminated by local host";
    case hci::kRepeatedAttempts: return "Repeated attempts";
    case hci::kPairingNotAllowed: return "Pairing not allowed";
    case hci::kUnknownLMPPDU: return "Unknown LMP PDU";
    case hci::kUnsupportedRemoteFeature: return "Unsupported remote feature";
    case hci::kSCOOffsetRejected: return "SCO offset rejected";
    case hci::kSCOIntervalRejected: return "SCO interval rejected";
    case hci::kSCOAirModeRejected: return "SCO air mode rejected";
    case hci::kInvalidLMPOrLLParameters: return "Invalid LMP or LL parameters";
    case hci::kUnspecifiedError: return "Unspecified error";
    case hci::kUnsupportedLMPOrLLParameterValue: return "Unsupported LMP or LL parameter value";
    case hci::kRoleChangeNotAllowed: return "Role change not allowed";
    case hci::kLMPOrLLResponseTimeout: return "LMP or LL response timeout";
    case hci::kLMPErrorTransactionCollision: return "LMP error transaction collision";
    case hci::kLMPPDUNotAllowed: return "LMP PDU not allowed";
    case hci::kEncryptionModeNotAcceptable: return "Encryption mode not acceptable";
    case hci::kLinkKeyCannotBeChanged: return "Link key cannot be changed";
    case hci::kRequestedQoSNotSupported: return "Requested QoS not supported";
    case hci::kInstantPassed: return "Instant passed";
    case hci::kPairingWithUnitKeyNotSupported: return "Pairing with unit key not supported";
    case hci::kDifferentTransactionCollision: return "Different transaction collision";
    case hci::kQoSUnacceptableParameter: return "QoS unacceptable parameter";
    case hci::kQoSRejected: return "QoS rejected";
    case hci::kChannelClassificationNotSupported: return "Channel classification not supported";
    case hci::kInsufficientSecurity: return "Insufficient security";
    case hci::kParameterOutOfMandatoryRange: return "Parameter out of mandatory range";
    case hci::kRoleSwitchPending: return "Role switch pending";
    case hci::kReservedSlotViolation: return "Reserved slot violation";
    case hci::kRoleSwitchFailed: return "Role switch failed";
    case hci::kExtendedInquiryResponseTooLarge: return "Extended inquiry response too large";
    case hci::kSecureSimplePairingNotSupportedByHost: return "Secure simple pairing not supported by host";
    case hci::kHostBusyPairing: return "Host busy pairing";
    case hci::kConnectionRejectedNoSuitableChannelFound: return "Connection rejected: no suitable channel found";
    case hci::kControllerBusy: return "Controller busy";
    case hci::kUnacceptableConnectionParameters: return "Unacceptable connection parameters";
    case hci::kDirectedAdvertisingTimeout: return "Directed advertising timeout";
    case hci::kConnectionTerminatedMICFailure: return "Connection terminated: MIC failure";
    case hci::kConnectionFailedToBeEstablished: return "Connection failed to be established";
    case hci::kMACConnectionFailed: return "MAC connection failed";
    case hci::kCoarseClockAdjustmentRejected: return "Coarse clock adjustment rejected";
    case hci::kType0SubmapNotDefined: return "Type 0 submap not defined";
    case hci::kUnknownAdvertisingIdentifier: return "Unknown advertising identifier";
    case hci::kLimitReached: return "Limit reached";
    case hci::kOperationCancelledByHost: return "Operation cancelled by host";

    case hci::kReserved0: break;
    case hci::kReserved1: break;
    case hci::kReserved2: break;
    default: break;
  };
  return "Unknown status";
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
