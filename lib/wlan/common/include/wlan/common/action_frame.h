// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>
#include <wlan/common/bitfield.h>
#include <wlan/common/element.h>
#include <wlan/common/macaddr.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>

namespace wlan {

// IEEE Std 802.11-2016, 9.4.1.14
class BlockAckParameters : public common::BitField<uint16_t> {
   public:
    WLAN_BIT_FIELD(amsdu, 0, 1);
    WLAN_BIT_FIELD(policy, 1, 1);
    WLAN_BIT_FIELD(tid, 2, 4);
    WLAN_BIT_FIELD(buffer_size, 6, 10);

    enum BlockAckPolicy {
        kDelayed = 0,
        kImmediate = 1,
    };
};

// IEEE Std 802.11-2016, 9.3.1.8.2, Figure 9-28
// Note the use of this field is obsolete, and the spec may remove it.
// Also note some APs fill this with zero values.
class BlockAckStartingSequenceControl : public common::BitField<uint16_t> {
   public:
    WLAN_BIT_FIELD(fragment, 0, 4);
    WLAN_BIT_FIELD(starting_seq, 4, 12);
};

// IEEE Std 802.11-2016, 9.4.1.11 Table 9-47
namespace action {
enum Category : uint8_t {
    kSpectrumManagement = 0,
    kQoS = 1,
    kDls = 2,
    kBlockAck = 3,
    kPublic = 4,
    kRadioMeasurement = 5,
    kFastBssTransition = 6,
    kHt = 7,
    kSaQuery = 8,
    kProtectedDualOfPublicAction = 9,
    kWnm = 10,
    kUnprotectedWnm = 11,
    kTdls = 12,
    kMesh = 13,
    kMultihop = 14,
    kSelfProtected = 15,
    kDmg = 16,
    // kReservedWfa = 17,
    kFastSessionTransfer = 18,
    kRobustAvStreaming = 19,
    kUnprotectedDmg = 20,
    kVht = 21,
    // 21 - 125 Reserved
    kVendorSpecificProtected = 126,
    kVendorSpecific = 127,
    // 128 - 255 Error
};

enum BaAction : uint8_t {
    kAddBaRequest = 0,   // Add Block Ack Request
    kAddBaResponse = 1,  // Add Block Ack Response
    kDelBa = 2,          // Delete Block Ack
    // 3 - 255 Reserved
};

}  // namespace action

// IEEE Std 802.11-2016, 9.6.5.2
// TODO(hahnr): Rename all these to *Hdr rather than *Frame.
struct AddBaRequestFrame {
    static constexpr action::BaAction BlockAckAction() { return action::BaAction::kAddBaRequest; }

    uint8_t dialog_token;  // IEEE Std 802.11-2016, 9.4.1.12
    BlockAckParameters params;
    uint16_t timeout;  // TU. 9.4.1.15
    BlockAckStartingSequenceControl seq_ctrl;

    // TODO(porce): Evaluate the use cases and support optional fields.
    // GCR Group Address element
    // Multi-band
    // TCLAS
    // ADDBA Extension

    constexpr size_t len() const { return sizeof(*this); }
} __PACKED;

// IEEE Std 802.11-2016, 9.6.5.3
struct AddBaResponseFrame {
    static constexpr action::BaAction BlockAckAction() { return action::BaAction::kAddBaResponse; }

    uint8_t dialog_token;       // IEEE Std 802.11-2016, 9.4.1.12
    uint16_t status_code;       // TODO(porce): Refactor out mac_frame.h and use type StatusCode.
    BlockAckParameters params;  // 9.4.1.9
    uint16_t timeout;           // 9.4.1.15

    // TODO(porce): Evaluate the use cases and support optional fields.
    // GCR Group Address element
    // Multi-band
    // TCLAS
    // ADDBA Extension

    constexpr size_t len() const { return sizeof(*this); }
} __PACKED;

// IEEE Std 802.11-2016, 9.4.1.16
class BlockAckDelBaParameters : public common::BitField<uint16_t> {
   public:
    // WLAN_BIT_FIELD(reserved, 0, 11);
    WLAN_BIT_FIELD(initiator, 11, 1);
    WLAN_BIT_FIELD(tid, 12, 4);
};

// IEEE Std 802.11-2016, 9.6.5.4
struct DelBaFrame {
    static constexpr action::BaAction BlockAckAction() { return action::BaAction::kDelBa; }

    BlockAckDelBaParameters params;
    uint16_t reason_code;         // TODO(porce): Refactor mac_frame.h and use ReasonCode type
    GcrGroupAddressElement addr;  // Info Element with ID kGcrGroupAddress

    // TODO(porce): Evaluate the use cases and support optional fields.
    // Multi-band
    // TCLAS

    constexpr size_t len() const { return sizeof(*this); }
} __PACKED;

// IEEE Std 802.11-2016, 9.6.5.1
struct ActionFrameBlockAck {
    static constexpr action::Category ActionCategory() { return action::Category::kBlockAck; }

    action::BaAction action;

    constexpr size_t len() const { return sizeof(*this); }
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.14
struct ActionFrame {
    static constexpr uint8_t Subtype() { return 0x0D; }
    uint8_t category;

    constexpr size_t len() const { return sizeof(*this); }
} __PACKED;

}  // namespace wlan
