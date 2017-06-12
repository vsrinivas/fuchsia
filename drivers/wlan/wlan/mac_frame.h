// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "element.h"

#include <drivers/wifi/common/bitfield.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <mxtl/type_support.h>

#include <cstdint>

namespace wlan {

static constexpr mx_duration_t TimeUnit = MX_USEC(1024);
template <typename T>
static inline constexpr mx_duration_t WLAN_TU(T n) {
    static_assert(mxtl::is_unsigned_integer<T>::value, "Time unit must be an unsigned integer");
    return TimeUnit * n;
}

// IEEE Std 802.11-2016, 9.2,4,1.1
class FrameControl : public common::BitField<uint16_t> {
  public:
    constexpr explicit FrameControl(uint16_t fc) : common::BitField<uint16_t>(fc) {}
    constexpr FrameControl() = default;

    WLAN_BIT_FIELD(protocol_version, 0, 2);
    WLAN_BIT_FIELD(type, 2, 2);
    WLAN_BIT_FIELD(subtype, 4, 4);
    WLAN_BIT_FIELD(to_ds, 8, 1);
    WLAN_BIT_FIELD(from_ds, 9, 1);
    WLAN_BIT_FIELD(more_frag, 10, 1);
    WLAN_BIT_FIELD(retry, 11, 1);
    WLAN_BIT_FIELD(pwr_mgmt, 12, 1);
    WLAN_BIT_FIELD(more_data, 13, 1);
    WLAN_BIT_FIELD(protected_frame, 14, 1);
    WLAN_BIT_FIELD(htc_order, 15, 1);

    // For type == Control and subtype == Control Frame Extension
    WLAN_BIT_FIELD(cf_extension, 8, 4);
};

// Frame types and subtypes
// IEEE Std 802.11-2016, 9.2.4.1.3

enum FrameType : uint8_t {
    kManagement = 0x00,
    kControl = 0x01,
    kData = 0x02,
    kExtension = 0x03,
};

enum ManagementSubtype : uint8_t {
    kAssociationRequest = 0x00,
    kAssociationResponse = 0x01,
    kReassociationRequest = 0x02,
    kReassociationResponse = 0x03,
    kProbeRequest = 0x04,
    kProbeResponse = 0x05,
    kTimingAdvertisement = 0x06,
    kBeacon = 0x08,
    kAtim = 0x09,
    kDisassociation = 0x0a,
    kAuthentication = 0x0b,
    kDeauthentication = 0x0c,
    kAction = 0x0d,
    kActionNoAck = 0x0e,
};

enum ControlSubtype : uint8_t {
    kBeamformingReportPoll = 0x04,
    kVhtNdpAnnouncement = 0x05,
    kControlFrameExtension = 0x06,
    kControlWrapper = 0x07,
    kBlockAckRequest = 0x08,
    kBlockAck = 0x09,
    kPsPoll = 0x0a,
    kRts = 0x0b,
    kCts = 0x0c,
    kAck = 0x0d,
    kCfEnd = 0x0e,
    kCfEndCfAck = 0x0f,
};

// The subtypes for Data frames are essentially composed from the following
// bitmask.
enum DataSubtype : uint8_t {
    kCfAck =  (1 << 0),
    kCfPoll = (1 << 1),
    kNull =   (1 << 2),
    kQos =    (1 << 3),
};

// IEEE Std 802.11-2016, 9.2.4.4
class SequenceControl : public common::BitField<uint16_t> {
  public:
    WLAN_BIT_FIELD(frag, 0, 4);
    WLAN_BIT_FIELD(seq, 4, 12);
};

// IEEE Std 802.11-2016, 9.2.4.6
class HtControl : public common::BitField<uint32_t> {
  public:
    WLAN_BIT_FIELD(vht, 0, 1);

    // Structure of this middle section is defined in 9.2.4.6.2 for HT, and 9.2.4.6.3 for VHT.
    // TODO(tkilbourn): define bitfield structures for each of these variants
    WLAN_BIT_FIELD(middle, 1, 29);
    WLAN_BIT_FIELD(ac_constraint, 30, 1);
    WLAN_BIT_FIELD(rdg_more_ppdu, 31, 1);
};

// IEEE Std 802.11-2016, 9.4.1.4
class CapabilityInfo : public common::BitField<uint16_t> {
  public:
    WLAN_BIT_FIELD(ess, 0, 1);
    WLAN_BIT_FIELD(ibss, 1, 1);
    WLAN_BIT_FIELD(cf_pollable, 2, 1);
    WLAN_BIT_FIELD(cf_poll_req, 3, 1);
    WLAN_BIT_FIELD(privacy, 4, 1);
    WLAN_BIT_FIELD(short_preamble, 5, 1);
    WLAN_BIT_FIELD(spectrum_mgmt, 8, 1);
    WLAN_BIT_FIELD(qos, 9, 1);
    WLAN_BIT_FIELD(short_slot_time, 10, 1);
    WLAN_BIT_FIELD(apsd, 11, 1);
    WLAN_BIT_FIELD(radio_msmt, 12, 1);
    WLAN_BIT_FIELD(delayed_block_ack, 14, 1);
    WLAN_BIT_FIELD(immediate_block_ack, 15, 1);
};

// IEEE Std 802.11-2016, 9.3.3.2
struct MgmtFrameHeader {
    FrameControl fc;
    uint16_t duration;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint8_t addr3[6];
    SequenceControl sc;
    // This field may not be present! Use the size() method to determine the
    // offset to the frame body.
    HtControl htc;

    bool has_ht_control() const { return fc.htc_order(); }
    size_t size() const {
        return sizeof(MgmtFrameHeader) - (has_ht_control() ? 0 : sizeof(HtControl));
    }
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.3
struct Beacon {
    // 9.4.1.10
    uint64_t timestamp;
    // 9.4.1.3
    uint16_t beacon_interval;
    // 9.4.1.4
    CapabilityInfo cap;

    uint8_t elements[];
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.10
struct ProbeRequest {
    bool Validate(size_t len);

    uint8_t elements[];
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.11
struct ProbeResponse {
    // 9.4.1.10
    uint64_t timestamp;
    // 9.4.1.3
    uint16_t beacon_interval;
    // 9.4.1.4
    CapabilityInfo cap;

    uint8_t elements[];
} __PACKED;

}  // namespace wlan
