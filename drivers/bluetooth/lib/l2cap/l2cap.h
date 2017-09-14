// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <cstdint>

#include <zircon/compiler.h>

namespace bluetooth {
namespace l2cap {

// L2CAP channel identifier uniquely identifies fixed and connection-oriented channels over a
// logical link.
using ChannelId = uint16_t;

// Fixed channel identifiers used in BR/EDR & AMP (i.e. ACL-U, ASB-U, and AMP-U logical links)
// (see Core Spec v5.0, Vol 3, Part A, Section 2.1)
constexpr ChannelId kSignalingChannelId = 0x0001;
constexpr ChannelId kConnectionlessChannelId = 0x0002;
constexpr ChannelId kAMPManagerChannelId = 0x0003;
constexpr ChannelId kSMChannelId = 0x0007;
constexpr ChannelId kAMPTestManagerChannelId = 0x003F;

// Fixed channel identifiers used in LE
// (see Core Spec v5.0, Vol 3, Part A, Section 2.1)
constexpr ChannelId kATTChannelId = 0x0004;
constexpr ChannelId kLESignalingChannelId = 0x0005;
constexpr ChannelId kSMPChannelId = 0x0006;

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

}  // namespace l2cap
}  // namespace bluetooth
