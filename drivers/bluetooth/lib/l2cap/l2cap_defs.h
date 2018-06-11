// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_DEFS_H_
#define GARNET_DRIVERS_BLUETOOTH_LIB_L2CAP_L2CAP_DEFS_H_

// clang-format off

#include <cstdint>

#include <zircon/compiler.h>

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

enum RejectReason : uint16_t {
  kNotUnderstood = 0x0000,
  kSignalingMTUExceeded = 0x0001,
  kInvalidCID = 0x0002,
};

enum ConnectionParameterUpdateResult : uint16_t {
  kAccepted = 0x0000,
  kRejected = 0x0001,
};

enum LECreditBasedConnectionResult : uint16_t {
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
  // See RejectReason for possible values.
  uint16_t reason;

  // Up to 4 octets of optional data (see Vol 3, Part A, Section 4.1)
  uint8_t data[kCommandRejectMaxDataLength];
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
