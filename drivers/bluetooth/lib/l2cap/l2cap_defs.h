// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_DEFS_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_DEFS_H_

// clang-format off

#include <cstdint>

#include <zircon/compiler.h>

#include "lib/fxl/macros.h"

namespace btlib {
namespace l2cap {

// L2CAP channel identifier uniquely identifies fixed and connection-oriented channels over a
// logical link.
using ChannelId = uint16_t;

// Fixed channel identifiers used in BR/EDR & AMP (i.e. ACL-U, ASB-U, and AMP-U logical links)
// (see Core Spec v5.0, Vol 3, Part A, Section 2.1)
constexpr ChannelId kSignalingChannelId = 0x0001;
constexpr ChannelId kConnectionlessChannelId = 0x0002;
constexpr ChannelId kAMPManagerChannelId = 0x0003;
constexpr ChannelId kSMPChannelId = 0x0007;
constexpr ChannelId kAMPTestManagerChannelId = 0x003F;

// Fixed channel identifiers used in LE
// (see Core Spec v5.0, Vol 3, Part A, Section 2.1)
constexpr ChannelId kATTChannelId = 0x0004;
constexpr ChannelId kLESignalingChannelId = 0x0005;
constexpr ChannelId kLESMPChannelId = 0x0006;

// Basic L2CAP header. This corresponds to the header used in a B-frame (Basic Information Frame)
// and is the basis of all other frame types.
struct BasicHeader {
  uint16_t length;
  ChannelId channel_id;
} __PACKED;

// The L2CAP MTU defines the maximum SDU size and is asymmetric. The following are the minimum and
// default MTU sizes that a L2CAP implementation must support (see Core Spec v5.0, Vol 3, Part A,
// Section 5.1).
constexpr uint16_t kDefaultMTU = 672;
constexpr uint16_t kMinACLMTU = 48;
constexpr uint16_t kMinLEMTU = 23;

// The maximum length of a L2CAP B-frame information payload.
constexpr uint16_t kMaxBasicFramePayloadSize = 65535;

// Signaling packet formats (Core Spec v5.0, Vol 3, Part A, Section 4):

using CommandCode = uint8_t;

enum class RejectReason : uint16_t {
  kNotUnderstood = 0x0000,
  kSignalingMTUExceeded = 0x0001,
  kInvalidCID = 0x0002,
};

// Results field in Connection Response and Create Channel Response
enum class ConnectionResult : uint16_t {
  kSuccess = 0x0000,
  kPending = 0x0001,
  kPSMNotSupported = 0x0002,
  kSecurityBlock = 0x0003,
  kNoResources = 0x0004,
  kControllerNotSupported = 0x0005,  // for Create Channel only
  kInvalidSourceCID = 0x0006,
  kSourceCIDAlreadyAllocated = 0x0007,
};

enum class ConnectionStatus : uint16_t {
  kNoInfoAvailable = 0x0000,
  kAuthenticationPending = 0x0001,
  kAuthorizationPending = 0x0002,
};

// Flags field in Configuration request and response, continuation bit mask
constexpr uint16_t kConfigurationContinuation = 0x0001;

enum class ConfigurationResult : uint16_t {
  kSuccess = 0x0000,
  kUnacceptableParameters = 0x0001,
  kRejected = 0x0002,
  kUnknownOptions = 0x0003,
  kPending = 0x0004,
  kFlowSpecRejected = 0x0005,
};

enum class InformationType : uint16_t {
  kConnectionlessMTU = 0x0001,
  kExtendedFeaturesSupported = 0x0002,
  kFixedChannelsSupported = 0x0003,
};

enum class InformationResult : uint16_t {
  kSuccess = 0x0000,
  kNotSupported = 0x0001,
};

// Bit masks for Extended Features Supported in the Information Response data
// field (Vol 3, Part A, Section 4.12)
enum class ExtendFeatures : uint32_t {
  kFlowControl = 1 << 0,
  kRetransmission = 1 << 1,
  kBidirectionalQoS = 1 << 2,
  kEnhancedRetransmission = 1 << 3,
  kStreaming = 1 << 4,
  kFCSOption = 1 << 5,
  kExtendedFlowSpecification = 1 << 6,
  kFixedChannels = 1 << 7,
  kExtendedWindowSize = 1 << 8,
  kUnicastConnectionlessDataRx = 1 << 9,
};

// Bit masks for Fixed Channels Supported in the Information Response data
// field (Vol 3, Part A, Section 4.12)
enum class FixedChannelsSupported : uint64_t {
  kNull = 1ULL << 0,
  kSignaling = 1ULL << 1,
  kConnectionless = 1ULL << 2,
  kAMPManager = 1ULL << 3,
  kATT = 1ULL << 4,
  kLESignaling = 1ULL << 5,
  kSMP = 1ULL << 6,
  kSM = 1ULL << 7,
  kAMPTestManager = 1ULL << 63,
};

enum class ConnectionParameterUpdateResult : uint16_t {
  kAccepted = 0x0000,
  kRejected = 0x0001,
};

enum class LECreditBasedConnectionResult : uint16_t {
  kSuccess = 0x0000,
  kPSMNotSupported = 0x0002,
  kNoResources = 0x0004,
  kInsufficientAuthentication = 0x0005,
  kInsufficientAuthorization = 0x0006,
  kInsufficientEncryptionKeySize = 0x0007,
  kInsufficientEncryption = 0x0008,
  kInvalidSourceCID = 0x0009,
  kSourceCIDAlreadyAllocated = 0x000A,
  kUnacceptableParameters = 0x000B,
};

// Identifier assigned to each signaling transaction. This is used to match each
// signaling channel request with a response.
using CommandId = uint8_t;

constexpr CommandId kInvalidCommandId = 0x00;

// Signaling command header.
struct CommandHeader {
  CommandCode code;
  CommandId id;
  uint16_t length;  // Length of the remaining payload
} __PACKED;

// ACL-U & LE-U
constexpr CommandCode kCommandRejectCode = 0x01;
constexpr size_t kCommandRejectMaxDataLength = 4;
struct CommandRejectPayload {
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(CommandRejectPayload);

  // See RejectReason for possible values.
  uint16_t reason;

  // Up to 4 octets of optional data (see Vol 3, Part A, Section 4.1)
  uint8_t data[];
} __PACKED;

// ACL-U
constexpr CommandCode kConnectionRequest = 0x02;
struct ConnectionRequestPayload {
  uint16_t psm;
  ChannelId src_cid;
} __PACKED;

// ACL-U
constexpr CommandCode kConnectionResponse = 0x03;
struct ConnectionResponsePayload {
  ChannelId dst_cid;
  ChannelId src_cid;
  ConnectionResult result;
  ConnectionStatus status;
} __PACKED;

// ACL-U
constexpr CommandCode kConfigurationRequest = 0x04;
constexpr size_t kConfigurationOptionMaxDataLength = 22;

// Element of configuration payload data (see Vol 3, Part A, Section 5)
struct ConfigurationOption {
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(ConfigurationOption);

  uint8_t type;
  uint8_t length;

  // Up to 22 octets of data
  uint8_t data[];
} __PACKED;

struct ConfigurationRequestPayload {
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(ConfigurationRequestPayload);

  ChannelId dst_cid;
  uint16_t flags;

  // Followed by zero or more configuration options of varying length
  uint8_t data[];
} __PACKED;

// ACL-U
constexpr CommandCode kConfigurationResponse = 0x05;
struct ConfigurationResponsePayload {
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(ConfigurationResponsePayload);

  ChannelId src_cid;
  uint16_t flags;
  ConfigurationResult result;

  // Followed by zero or more configuration options of varying length
  uint8_t data[];
} __PACKED;

// ACL-U & LE-U
constexpr CommandCode kDisconnectRequest = 0x06;
struct DisconnectRequestPayload {
  ChannelId dst_cid;
  ChannelId src_cid;
} __PACKED;

// ACL-U & LE-U
constexpr CommandCode kDisconnectResponse = 0x07;
struct DisconnectResponsePayload {
  ChannelId dst_cid;
  ChannelId src_cid;
} __PACKED;

// ACL-U
constexpr CommandCode kEchoRequest = 0x08;

// ACL-U
constexpr CommandCode kEchoResponse = 0x09;

// ACL-U
constexpr CommandCode kInformationRequest = 0x0A;
struct InformationRequestPayload {
  InformationType type;
} __PACKED;

// ACL-U
constexpr CommandCode kInformationResponse = 0x0B;
constexpr size_t kInformationResponseMaxDataLength = 8;
struct InformationResponsePayload {
  FXL_DISALLOW_IMPLICIT_CONSTRUCTORS(InformationResponsePayload);

  InformationType type;
  InformationResult result;

  // Up to 8 octets of optional data (see Vol 3, Part A, Section 4.11)
  uint8_t data[];
} __PACKED;

// LE-U
constexpr CommandCode kConnectionParameterUpdateRequest = 0x12;
struct ConnectionParameterUpdateRequestPayload {
  uint16_t interval_min;
  uint16_t interval_max;
  uint16_t slave_latency;
  uint16_t timeout_multiplier;
} __PACKED;

// LE-U
constexpr CommandCode kConnectionParameterUpdateResponse = 0x13;
struct ConnectionParameterUpdateResponsePayload {
  ConnectionParameterUpdateResult result;
} __PACKED;

// LE-U
constexpr CommandCode kLECreditBasedConnectionRequest = 0x14;
struct LECreditBasedConnectionRequestPayload {
  uint16_t le_psm;
  ChannelId src_cid;
  uint16_t mtu;  // Max. SDU size
  uint16_t mps;  // Max. PDU size
  uint16_t initial_credits;
} __PACKED;

// LE-U
constexpr CommandCode kLECreditBasedConnectionResponse = 0x15;
struct LECreditBasedConnectionResponsePayload {
  ChannelId dst_cid;
  uint16_t mtu;  // Max. SDU size
  uint16_t mps;  // Max. PDU size
  uint16_t initial_credits;
  LECreditBasedConnectionResult result;
} __PACKED;

// LE-U
constexpr CommandCode kLEFlowControlCredit = 0x16;
struct LEFlowControlCreditParams {
  ChannelId cid;
  uint16_t credits;
} __PACKED;

}  // namespace l2cap
}  // namespace btlib

#endif  // GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_DEFS_H_
