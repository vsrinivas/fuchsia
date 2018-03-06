// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "util.h"

#include "lib/fxl/logging.h"

namespace btlib {
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

// clang-format off
std::string StatusCodeToString(hci::StatusCode code) {
  switch (code) {
    case hci::kSuccess: return "success";
    case hci::kUnknownCommand: return "unknown command";
    case hci::kUnknownConnectionId: return "unknown connection ID";
    case hci::kHardwareFailure: return "hardware failure";
    case hci::kPageTimeout: return "page timeout";
    case hci::kAuthenticationFailure: return "authentication failure";
    case hci::kPinOrKeyMissing: return "pin or key missing";
    case hci::kMemoryCapacityExceeded: return "memory capacity exceeded";
    case hci::kConnectionTimeout: return "connection timeout";
    case hci::kConnectionLimitExceeded: return "connection limit exceeded";
    case hci::kSynchronousConnectionLimitExceeded: return "synchronous connection limit exceeded";
    case hci::kConnectionAlreadyExists: return "connection already exists";
    case hci::kCommandDisallowed: return "command disallowed";
    case hci::kConnectionRejectedLimitedResources: return "connection rejected: limited resources";
    case hci::kConnectionRejectedSecurity: return "connection rejected: security";
    case hci::kConnectionRejectedBadBdAddr: return "connection rejected: bad BD_ADDR";
    case hci::kConnectionAcceptTimeoutExceeded: return "connection accept timeout exceeded";
    case hci::kUnsupportedFeatureOrParameter: return "unsupported feature or parameter";
    case hci::kInvalidHCICommandParameters: return "invalid HCI command parameters";
    case hci::kRemoteUserTerminatedConnection: return "remote user terminated connection";
    case hci::kRemoteDeviceTerminatedConnectionLowResources: return "remote device terminated connection: low resources";
    case hci::kRemoteDeviceTerminatedConnectionPowerOff: return "remote device rerminated connection: power off";
    case hci::kConnectionTerminatedByLocalHost: return "connection terminated by local host";
    case hci::kRepeatedAttempts: return "repeated attempts";
    case hci::kPairingNotAllowed: return "pairing not allowed";
    case hci::kUnknownLMPPDU: return "unknown LMP PDU";
    case hci::kUnsupportedRemoteFeature: return "unsupported remote feature";
    case hci::kSCOOffsetRejected: return "SCO offset rejected";
    case hci::kSCOIntervalRejected: return "SCO interval rejected";
    case hci::kSCOAirModeRejected: return "SCO air mode rejected";
    case hci::kInvalidLMPOrLLParameters: return "invalid LMP or LL parameters";
    case hci::kUnspecifiedError: return "unspecified error";
    case hci::kUnsupportedLMPOrLLParameterValue: return "unsupported LMP or LL parameter value";
    case hci::kRoleChangeNotAllowed: return "role change not allowed";
    case hci::kLMPOrLLResponseTimeout: return "LMP or LL response timeout";
    case hci::kLMPErrorTransactionCollision: return "LMP error transaction collision";
    case hci::kLMPPDUNotAllowed: return "LMP PDU not allowed";
    case hci::kEncryptionModeNotAcceptable: return "encryption mode not acceptable";
    case hci::kLinkKeyCannotBeChanged: return "link key cannot be changed";
    case hci::kRequestedQoSNotSupported: return "requested QoS not supported";
    case hci::kInstantPassed: return "instant passed";
    case hci::kPairingWithUnitKeyNotSupported: return "pairing with unit key not supported";
    case hci::kDifferentTransactionCollision: return "different transaction collision";
    case hci::kQoSUnacceptableParameter: return "QoS unacceptable parameter";
    case hci::kQoSRejected: return "QoS rejected";
    case hci::kChannelClassificationNotSupported: return "channel classification not supported";
    case hci::kInsufficientSecurity: return "insufficient security";
    case hci::kParameterOutOfMandatoryRange: return "parameter out of mandatory range";
    case hci::kRoleSwitchPending: return "role switch pending";
    case hci::kReservedSlotViolation: return "reserved slot violation";
    case hci::kRoleSwitchFailed: return "role switch failed";
    case hci::kExtendedInquiryResponseTooLarge: return "extended inquiry response too large";
    case hci::kSecureSimplePairingNotSupportedByHost: return "secure simple pairing not supported by host";
    case hci::kHostBusyPairing: return "host busy pairing";
    case hci::kConnectionRejectedNoSuitableChannelFound: return "connection rejected: no suitable channel found";
    case hci::kControllerBusy: return "controller busy";
    case hci::kUnacceptableConnectionParameters: return "unacceptable connection parameters";
    case hci::kDirectedAdvertisingTimeout: return "directed advertising timeout";
    case hci::kConnectionTerminatedMICFailure: return "connection terminated: MIC failure";
    case hci::kConnectionFailedToBeEstablished: return "connection failed to be established";
    case hci::kMACConnectionFailed: return "MAC connection failed";
    case hci::kCoarseClockAdjustmentRejected: return "coarse clock adjustment rejected";
    case hci::kType0SubmapNotDefined: return "type 0 submap not defined";
    case hci::kUnknownAdvertisingIdentifier: return "unknown advertising identifier";
    case hci::kLimitReached: return "limit reached";
    case hci::kOperationCancelledByHost: return "operation cancelled by host";
    default: break;
  };
  return "unknown status";
}
// clang-format on

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
}  // namespace btlib
