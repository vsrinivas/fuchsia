// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>
#include <wlan/common/bitfield.h>
#include <wlan/common/element_id.h>
#include <wlan/common/logging.h>
#include <wlan/common/macaddr.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace wlan {

// IEEE Std 802.11-2016, 9.4.2.1
struct ElementHeader {
    uint8_t id;
    uint8_t len;
} __PACKED;

template <typename E, uint8_t ID> struct Element {
    static constexpr uint8_t element_id() { return ID; }
    size_t body_len() const { return static_cast<const E*>(this)->hdr.len; }
    size_t len() const { return sizeof(ElementHeader) + body_len(); }

    bool is_len_valid() const {
        // E::kMinLen and E::kMaxLen captures the range of the IE body length, excluding the IE
        // header whose size is fixed to 2 octets.
        if (body_len() >= E::kMinLen && body_len() <= E::kMaxLen) return true;

        // Crush dark arts.
        debugbcn(
            "rxed Invalid IE: ID %2d elem_len %2zu body_len %3zu (not in range "
            "[%3zu:%3zu]\n",
            E::element_id(), len(), body_len(), E::kMinLen, E::kMaxLen);
        return false;
    }

    bool is_valid() const { return is_len_valid(); }
};

// An ElementReader can be used to retrieve Elements from a Management frame. The peek() method will
// read the ElementHeader without advancing the iterator. The caller may then use the id in the
// header and call read() to retrieve the next Element. is_valid() will return false after reaching
// the end or parse errors. It is an error to call read() for a type different than the one
// specified in the ElementHeader.
class ElementReader {
   public:
    ElementReader(const uint8_t* buf, size_t len);

    bool is_valid() const;
    size_t offset() const { return offset_; }
    const ElementHeader* peek() const;
    void skip(size_t offset) { offset_ += offset; }
    void skip(const ElementHeader& hdr) { offset_ += sizeof(ElementHeader) + hdr.len; }

    template <typename E> const E* read() {
        static_assert(fbl::is_base_of<Element<E, E::element_id()>, E>::value,
                      "Only Elements may be retrieved.");
        if (!is_valid()) {
            debugbcn("IE validity test failed: ID %3u len_ %3zu offset_ %3zu elem_len %3zu\n",
                     E::element_id(), len_, offset_, NextElementLen());

            return nullptr;
        }

        if (offset_ + sizeof(E) > len_) {
            debugbcn(
                "IE validity test failed: ID %3u len_ %3zu offset_ %3zu elem_len %3zu sizeof(E) "
                "%3zu\n",
                E::element_id(), len_, offset_, NextElementLen(), sizeof(E));
            return nullptr;
        }

        const E* elt = reinterpret_cast<const E*>(buf_ + offset_);
        ZX_DEBUG_ASSERT(elt->hdr.id == E::element_id());
        if (!elt->is_valid()) return nullptr;

        offset_ += sizeof(ElementHeader) + elt->hdr.len;
        return elt;
    }

   private:
    size_t NextElementLen() const;

    const uint8_t* const buf_;
    const size_t len_;
    size_t offset_ = 0;
};

// An ElementWriter will serialize Elements into a buffer. The size() method will return the total
// length of the buffer.
class ElementWriter {
   public:
    ElementWriter(uint8_t* buf, size_t len);

    template <typename E, typename... Args> bool write(Args&&... args) {
        static_assert(fbl::is_base_of<Element<E, E::element_id()>, E>::value,
                      "Only Elements may be inserted.");
        if (offset_ >= len_) return false;

        size_t actual = 0;
        bool success =
            E::Create(buf_ + offset_, len_ - offset_, &actual, std::forward<Args>(args)...);
        if (!success) return false;

        auto elem = reinterpret_cast<const E*>(buf_ + offset_);
        if (!elem->is_valid()) {
            warnf("ElementWriter: IE %3u has invalid body length: %3u\n", E::element_id(),
                  elem->hdr.len);
        }

        offset_ += actual;
        ZX_DEBUG_ASSERT(offset_ <= len_);
        return true;
    }

    size_t size() const { return offset_; }

   private:
    uint8_t* buf_;
    const size_t len_;
    size_t offset_ = 0;
};

// IEEE Std 802.11-2016, 9.4.2.2
struct SsidElement : public Element<SsidElement, element_id::kSsid> {
    static bool Create(void* buf, size_t len, size_t* actual, const char* ssid);
    static const size_t kMinLen = 0;
    static const size_t kMaxLen = 32;

    ElementHeader hdr;
    char ssid[];
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.3
struct SupportedRatesElement : public Element<SupportedRatesElement, element_id::kSuppRates> {
    static bool Create(void* buf, size_t len, size_t* actual, const std::vector<uint8_t>& rates);
    static const size_t kMinLen = 1;
    static const size_t kMaxLen = 8;

    ElementHeader hdr;
    uint8_t rates[];
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.4
struct DsssParamSetElement : public Element<DsssParamSetElement, element_id::kDsssParamSet> {
    static bool Create(void* buf, size_t len, size_t* actual, uint8_t chan);
    static const size_t kMinLen = 1;
    static const size_t kMaxLen = 1;

    ElementHeader hdr;
    uint8_t current_chan;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.5
struct CfParamSetElement : public Element<CfParamSetElement, element_id::kCfParamSet> {
    static bool Create(void* buf, size_t len, size_t* actual, uint8_t count, uint8_t period,
                       uint16_t max_duration, uint16_t dur_remaining);
    static const size_t kMinLen = 6;
    static const size_t kMaxLen = 6;

    ElementHeader hdr;
    uint8_t count;
    uint8_t period;
    uint16_t max_duration;
    uint16_t dur_remaining;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.6
class BitmapControl : public common::BitField<uint8_t> {
   public:
    WLAN_BIT_FIELD(group_traffic_ind, 0, 1);
    WLAN_BIT_FIELD(offset, 1, 7);
};

// IEEE Std 802.11-2016, 9.4.2.6
struct TimElement : public Element<TimElement, element_id::kTim> {
    static bool Create(void* buf, size_t len, size_t* actual, uint8_t dtim_count,
                       uint8_t dtim_period, BitmapControl bmp_ctrl, const uint8_t* bmp,
                       size_t bmp_len);
    static const size_t kMinLenBmp = 1;
    static const size_t kMaxLenBmp = 251;
    static const size_t kFixedLenBody = 3;
    static const size_t kMinLen = kFixedLenBody + kMinLenBmp;
    static const size_t kMaxLen = kFixedLenBody + kMaxLenBmp;

    ElementHeader hdr;

    // body: fixed 3 bytes
    uint8_t dtim_count;
    uint8_t dtim_period;
    BitmapControl bmp_ctrl;

    // body: variable length 1-251 bytes.
    uint8_t bmp[];

    bool traffic_buffered(uint16_t aid) const;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.9. Figure 9-131, 9-132.
struct SubbandTriplet {
    uint8_t first_channel_number;
    uint8_t number_of_channels;
    uint8_t max_tx_power;  // dBm
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.9
struct CountryElement : public Element<CountryElement, element_id::kCountry> {
    static bool Create(void* buf, size_t len, size_t* actual, const uint8_t* country,
                       const std::vector<SubbandTriplet>& subbands);
    static const size_t kCountryLen = 3;
    static const size_t kMinLen = 3;  // TODO(porce): revisit the spec.
    static const size_t kMaxLen = 255;

    ElementHeader hdr;

    // TODO(NET-799): Validate dot11CountryString
    // Note, country octets is not a null-terminated string.
    // IEEE802.11-MIB Object Identifier 1.2.840.10036.1.1.1.23: dot11CountryString
    // First two octets is the two character country code defined in ISO/IEC 3166-1
    // The third octets
    // - ASCII space character: all environments
    // - ASCII 'O' : Outdoor environment only
    // - ASCII 'I' : Indoor environment only
    // - ASCII 'X' : Noncountry entity
    // - Binary value of the Operating Class table number. Annex E Table E-1 becomes 0x01.
    uint8_t country[kCountryLen];
    static_assert(sizeof(SubbandTriplet) == 3,
                  "Wireformat for SubbandTriplet is of length 3 octets.");

    // One or more SubbandTriplet, if dot11OperatingClassesRequired is false.
    // TODO(porce): Revisit for VHT, and if dot11OperatingClassesRequired is true.
    uint8_t triplets[];
    // Zero-padding, zero or one octect. Make the length of the CountryElement be even.
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.13
struct ExtendedSupportedRatesElement
    : public Element<ExtendedSupportedRatesElement, element_id::kExtSuppRates> {
    static bool Create(void* buf, size_t len, size_t* actual, const std::vector<uint8_t>& rates);
    static const size_t kMinLen = 1;
    static const size_t kMaxLen = 255;

    ElementHeader hdr;
    uint8_t rates[];
} __PACKED;

const uint16_t kEapolProtocolId = 0x888E;

// IEEE Std 802.11-2016, 9.4.2.25.1
// The MLME always forwards the RSNE and never requires to decode the element itself. Hence, support
// for accessing optional fields is left out and implemented only by the SME.
struct RsnElement : public Element<RsnElement, element_id::kRsn> {
    static bool Create(void* buf, size_t len, size_t* actual, const uint8_t* raw, size_t raw_len);
    static const size_t kMinLen = 2;
    static const size_t kMaxLen = 255;

    ElementHeader hdr;
    uint16_t version;
    uint8_t fields[];
} __PACKED;

// IEEE Std 802.11-2016, 9.4.1.17
class QosInfo : public common::BitField<uint8_t> {
   public:
    constexpr explicit QosInfo(uint8_t value) : common::BitField<uint8_t>(value) {}
    constexpr QosInfo() = default;

    // AP specific QoS Info structure: IEEE Std 802.11-2016, 9.4.1.17, Figure 9-82
    WLAN_BIT_FIELD(edca_param_set_update_count, 0, 4);
    WLAN_BIT_FIELD(qack, 4, 1);
    WLAN_BIT_FIELD(queue_request, 5, 1);
    WLAN_BIT_FIELD(txop_request, 6, 1);
    // 8th bit reserved

    // Non-AP STA specific QoS Info structure: IEEE Std 802.11-2016, 9.4.1.17, Figure 9-83
    WLAN_BIT_FIELD(ac_vo_uapsd_flag, 0, 1);
    WLAN_BIT_FIELD(ac_vi_uapsd_flag, 1, 1);
    WLAN_BIT_FIELD(ac_bk_uapsd_flag, 2, 1);
    WLAN_BIT_FIELD(ac_be_uapsd_flag, 3, 1);
    // qack already defined in AP specific structure.
    WLAN_BIT_FIELD(max_sp_len, 5, 1);
    WLAN_BIT_FIELD(more_data_ack, 6, 1);
    // 8th bit reserved
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30, Table 9-139
enum TsDirection : uint8_t {
    kUplink = 0,
    kDownlink = 1,
    kDirectLink = 2,
    kBidirectionalLink = 3,
};

// IEEE Std 802.11-2016, 9.4.2.30, Table 9-140
enum TsAccessPolicy : uint8_t {
    // 0 reserved
    kEdca = 1,
    kHccaSpca = 2,
    kMixedMode = 3,
};

// IEEE Std 802.11-2016, 9.4.2.30, Table 9-141
namespace ts_ack_policy {
enum TsAckPolicy : uint8_t {
    kNormalAck = 0,
    kNoAck = 1,
    // 2 reserved
    kBlockAck = 3,
};
}  // namespace ts_ack_policy

// IEEE Std 802.11-2016, 9.4.2.30, Table 9-142
// Only used if TsInfo's access policy uses EDCA.
// Schedule Setting depends on TsInfo's ASPD and schedule fields.
enum TsScheduleSetting : uint8_t {
    kNoSchedule = 0,
    kUnschedledApsd = 1,
    kScheduledPsmp_GcrSp = 2,
    kScheduledApsd = 3,
};

// IEEE Std 802.11-2016, 9.4.2.30, Figure 9-266
class TsInfoPart1 : public common::BitField<uint16_t> {
   public:
    WLAN_BIT_FIELD(traffic_type, 0, 1);
    WLAN_BIT_FIELD(tsid, 1, 4);
    WLAN_BIT_FIELD(direction, 5, 2);
    WLAN_BIT_FIELD(access_policy, 7, 2);
    WLAN_BIT_FIELD(aggregation, 9, 1);
    WLAN_BIT_FIELD(apsd, 10, 1);
    WLAN_BIT_FIELD(user_priority, 11, 3);
    WLAN_BIT_FIELD(ack_policy, 14, 2);
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30, Figure 9-266
class TsInfoPart2 : public common::BitField<uint8_t> {
   public:
    WLAN_BIT_FIELD(schedule, 0, 1);
    // Bit 17-23 reserved.
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30, Figure 9-266
// Note: In order to use a 3 byte packed struct, the TsInfo was split into two parts.
struct TsInfo {
    TsInfoPart1 p1;
    TsInfoPart2 p2;

    bool IsValidAggregation() const {
        if (p1.access_policy() == TsAccessPolicy::kHccaSpca) { return true; }
        return p1.access_policy() == TsAccessPolicy::kEdca && p2.schedule();
    }

    bool IsScheduleReserved() const { return p1.access_policy() != TsAccessPolicy::kEdca; }

    TsScheduleSetting GetScheduleSetting() const {
        return static_cast<TsScheduleSetting>(p1.apsd() | (p2.schedule() << 1));
    }
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30, Figure 9-267
struct NominalMsduSize : public common::BitField<uint16_t> {
    WLAN_BIT_FIELD(size, 0, 15);
    WLAN_BIT_FIELD(fixed, 15, 1);
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.30
struct TspecElement : public Element<TspecElement, element_id::kTspec> {
    // TODO(hahnr): The element will for now only be read by the AP when received from an associated
    // client and there is no need for providing a custom constructor yet.

    ElementHeader hdr;
    TsInfo ts_info;
    NominalMsduSize nominal_msdu_size;
    uint16_t max_msdu_size;
    uint32_t min_service_interval;
    uint32_t max_service_interval;
    uint32_t inactivity_interval;
    uint32_t suspension_interval;
    uint32_t service_start_time;
    uint32_t min_data_rate;
    uint32_t mean_data_rate;
    uint32_t peak_data_rate;
    uint32_t burst_size;
    uint32_t delay_bound;
    uint32_t min_phy_rate;
    uint16_t surplus_bw_allowance;
    uint16_t medium_time;

    // TODO(hahnr): Add min/mean/peak data rate support based on the provided fields.
    // TODO(hahnr): Add min PHY rate support based on the provided field.
    // TODO(hahnr): Add DMG support.
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.35
struct QosCapabilityElement : public Element<QosCapabilityElement, element_id::kQosCapability> {
    static bool Create(void* buf, size_t len, size_t* actual, const QosInfo& qos_info);

    ElementHeader hdr;
    QosInfo qos_info;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.2
// Note this is a field of HtCapabilities element.
class HtCapabilityInfo : public common::BitField<uint16_t> {
   public:
    constexpr explicit HtCapabilityInfo(uint16_t ht_cap_info)
        : common::BitField<uint16_t>(ht_cap_info) {}
    constexpr HtCapabilityInfo() = default;

    WLAN_BIT_FIELD(ldpc_coding_cap, 0, 1);
    WLAN_BIT_FIELD(chan_width_set, 1, 1);  // In spec: Supported Channel Width Set
    WLAN_BIT_FIELD(sm_power_save, 2, 2);   // Spatial Multiplexing Power Save
    WLAN_BIT_FIELD(greenfield, 4, 1);      // HT-Greenfield.
    WLAN_BIT_FIELD(short_gi_20, 5, 1);     // Short Guard Interval for 20 MHz
    WLAN_BIT_FIELD(short_gi_40, 6, 1);     // Short Guard Interval for 40 MHz
    WLAN_BIT_FIELD(tx_stbc, 7, 1);

    WLAN_BIT_FIELD(rx_stbc, 8, 2);             // maximum number of spatial streams. Up to 3.
    WLAN_BIT_FIELD(delayed_block_ack, 10, 1);  // HT-delayed Block Ack
    WLAN_BIT_FIELD(max_amsdu_len, 11, 1);
    WLAN_BIT_FIELD(dsss_in_40, 12, 1);  // DSSS/CCK Mode in 40 MHz
    WLAN_BIT_FIELD(reserved, 13, 1);
    WLAN_BIT_FIELD(intolerant_40, 14, 1);  // 40 MHz Intolerant
    WLAN_BIT_FIELD(lsig_txop_protect, 15, 1);

    enum ChanWidthSet {
        TWENTY_ONLY = 0,
        TWENTY_FORTY = 1,
    };

    enum SmPowerSave {
        STATIC = 0,
        DYNAMIC = 1,
        RESERVED = 2,
        DISABLED = 3,
    };

    enum MaxAmsduLen {
        OCTETS_3839 = 0,
        OCTETS_7935 = 1,
    };
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.3
class AmpduParams : public common::BitField<uint8_t> {
   public:
    constexpr explicit AmpduParams(uint8_t params) : common::BitField<uint8_t>(params) {}
    constexpr AmpduParams() = default;

    WLAN_BIT_FIELD(exponent, 0, 2);           // Maximum A-MPDU Length Exponent.
    WLAN_BIT_FIELD(min_start_spacing, 2, 3);  // Minimum MPDU Start Spacing.
    WLAN_BIT_FIELD(reserved, 5, 3);

    size_t max_ampdu_len() const { return (1 << (13 + exponent())) - 1; }

    enum MinMPDUStartSpacing {
        NO_RESTRICT = 0,
        QUARTER_USEC = 1,
        HALF_USEC = 2,
        ONE_USEC = 3,
        TWO_USEC = 4,
        FOUR_USEC = 5,
        EIGHT_USEC = 6,
        SIXTEEN_USEC = 7,
    };
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.4
class SupportedMcsRxMcsHead : public common::BitField<uint64_t> {
   public:
    constexpr explicit SupportedMcsRxMcsHead(uint64_t val) : common::BitField<uint64_t>(val) {}
    constexpr SupportedMcsRxMcsHead() = default;

    // HT-MCS table in IEEE Std 802.11-2016, Annex B.4.17.2
    // VHT-MCS tables in IEEE Std 802.11-2016, 21.5
    WLAN_BIT_FIELD(bitmask, 0, 64);
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.4
class SupportedMcsRxMcsTail : public common::BitField<uint32_t> {
   public:
    constexpr explicit SupportedMcsRxMcsTail(uint32_t val) : common::BitField<uint32_t>(val) {}
    constexpr SupportedMcsRxMcsTail() = default;

    WLAN_BIT_FIELD(bitmask, 0, 13);
    WLAN_BIT_FIELD(reserved1, 13, 3);
    WLAN_BIT_FIELD(highest_rate, 16, 10);  // Mbps. Rx Highest Supported Rate.
    WLAN_BIT_FIELD(reserved2, 26, 6);
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.4
class SupportedMcsTxMcs : public common::BitField<uint32_t> {
   public:
    constexpr explicit SupportedMcsTxMcs(uint32_t chunk) : common::BitField<uint32_t>(chunk) {}
    constexpr SupportedMcsTxMcs() = default;

    WLAN_BIT_FIELD(set_defined, 0, 1);  // Add 96 for the original bit location
    WLAN_BIT_FIELD(rx_diff, 1, 1);
    WLAN_BIT_FIELD(max_ss, 2, 2);
    WLAN_BIT_FIELD(ueqm, 4, 1);  // Transmit Unequal Modulation.
    WLAN_BIT_FIELD(reserved, 5, 27);

    uint8_t max_ss_human() const { return max_ss() + 1; }
    void set_max_ss_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_max_ss(num - 1);
    }
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.4
struct SupportedMcsSet {
    SupportedMcsRxMcsHead rx_mcs_head;
    SupportedMcsRxMcsTail rx_mcs_tail;
    SupportedMcsTxMcs tx_mcs;

    // TODO(porce): Implement accessors
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.5
class HtExtCapabilities : public common::BitField<uint16_t> {
   public:
    constexpr explicit HtExtCapabilities(uint16_t ht_ext_cap)
        : common::BitField<uint16_t>(ht_ext_cap) {}
    constexpr HtExtCapabilities() = default;

    WLAN_BIT_FIELD(pco, 0, 1);
    WLAN_BIT_FIELD(pco_transition, 1, 2);
    WLAN_BIT_FIELD(reserved1, 3, 5);
    WLAN_BIT_FIELD(mcs_feedback, 8, 2);
    WLAN_BIT_FIELD(htc_ht_support, 10, 1);
    WLAN_BIT_FIELD(rd_responder, 11, 1);
    WLAN_BIT_FIELD(reserved2, 12, 4);

    enum PcoTransitionTime {
        PCO_RESERVED = 0,  // Often translated as "No transition".
        PCO_400_USEC = 1,
        PCO_1500_USEC = 2,
        PCO_5000_USEC = 3,
    };

    enum McsFeedback {
        MCS_NOFEEDBACK = 0,
        MCS_RESERVED = 1,
        MCS_UNSOLICIED = 2,
        MCS_BOTH = 3,
    };
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56.6
class TxBfCapability : public common::BitField<uint32_t> {
   public:
    constexpr explicit TxBfCapability(uint32_t txbf_cap) : common::BitField<uint32_t>(txbf_cap) {}
    constexpr TxBfCapability() = default;

    WLAN_BIT_FIELD(implicit_rx, 0, 1);
    WLAN_BIT_FIELD(rx_stag_sounding, 1, 1);
    WLAN_BIT_FIELD(tx_stag_sounding, 2, 1);
    WLAN_BIT_FIELD(rx_ndp, 3, 1);
    WLAN_BIT_FIELD(tx_ndp, 4, 1);
    WLAN_BIT_FIELD(implicit, 5, 1);
    WLAN_BIT_FIELD(calibration, 6, 2);
    WLAN_BIT_FIELD(csi, 8, 1);  // Explicit CSI Transmit Beamforming.

    WLAN_BIT_FIELD(noncomp_steering, 9, 1);  // Explicit Noncompressed Steering
    WLAN_BIT_FIELD(comp_steering, 10, 1);    // Explicit Compressed Steering
    WLAN_BIT_FIELD(csi_feedback, 11, 2);
    WLAN_BIT_FIELD(noncomp_feedback, 13, 2);
    WLAN_BIT_FIELD(comp_feedback, 15, 2);
    WLAN_BIT_FIELD(min_grouping, 17, 2);
    WLAN_BIT_FIELD(csi_antennas, 19, 2);

    WLAN_BIT_FIELD(noncomp_steering_ants, 21, 2);
    WLAN_BIT_FIELD(comp_steering_ants, 23, 2);
    WLAN_BIT_FIELD(csi_rows, 25, 2);
    WLAN_BIT_FIELD(chan_estimation, 27, 2);
    WLAN_BIT_FIELD(reserved, 29, 3);

    enum Calibration {
        CALIBRATION_NONE = 0,
        CALIBRATION_RESPOND_NOINITIATE = 1,
        CALIBRATION_RESERVED = 2,
        CALIBRATION_RESPOND_INITIATE = 3,
    };

    enum Feedback {
        // Shared for csi_feedback, noncomp_feedback, comp_feedback
        FEEDBACK_NONE = 0,
        FEEDBACK_DELAYED = 1,
        FEEDBACK_IMMEDIATE = 2,
        FEEDBACK_DELAYED_IMMEDIATE = 3,
    };

    enum MinGroup {
        MIN_GROUP_ONE = 0,  // Meaning no grouping
        MIN_GROUP_ONE_TWO = 1,
        MIN_GROUP_ONE_FOUR = 2,
        MIN_GROUP_ONE_TWO_FOUR = 3,
    };

    uint8_t csi_antennas_human() const { return csi_antennas() + 1; }
    void set_csi_antennas_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_csi_antennas(num - 1);
    };

    uint8_t noncomp_steering_ants_human() const { return noncomp_steering_ants() + 1; }
    void set_noncomp_steering_ants_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_noncomp_steering_ants(num - 1);
    }

    uint8_t comp_steering_ants_human() const { return comp_steering_ants() + 1; }
    void set_comp_steering_ants_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_comp_steering_ants(num - 1);
    }

    uint8_t csi_rows_human() const { return csi_rows() + 1; }
    void set_csi_rows_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_csi_rows(num - 1);
    }

    uint8_t chan_estimation_human() const { return chan_estimation() + 1; }
    void set_chan_estimation_human(uint8_t num) {
        constexpr uint8_t kLowerbound = 1;
        constexpr uint8_t kUpperbound = 4;
        if (num < kLowerbound) num = kLowerbound;
        if (num > kUpperbound) num = kUpperbound;
        set_chan_estimation(num - 1);
    }
} __PACKED;

class AselCapability : public common::BitField<uint8_t> {
   public:
    constexpr explicit AselCapability(uint8_t asel_cap) : common::BitField<uint8_t>(asel_cap) {}
    constexpr AselCapability() = default;

    WLAN_BIT_FIELD(asel, 0, 1);
    WLAN_BIT_FIELD(csi_feedback_tx_asel, 1, 1);  // Explicit CSI Feedback based Transmit ASEL
    WLAN_BIT_FIELD(ant_idx_feedback_tx_asel, 2, 1);
    WLAN_BIT_FIELD(explicit_csi_feedback, 3, 1);
    WLAN_BIT_FIELD(antenna_idx_feedback, 4, 1);
    WLAN_BIT_FIELD(rx_asel, 5, 1);
    WLAN_BIT_FIELD(tx_sounding_ppdu, 6, 1);
    WLAN_BIT_FIELD(reserved, 7, 1);
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.56
struct HtCapabilities : public Element<HtCapabilities, element_id::kHtCapabilities> {
    static bool Create(void* buf, size_t len, size_t* actual, HtCapabilityInfo ht_cap_info,
                       AmpduParams ampdu_params, SupportedMcsSet mcs_set,
                       HtExtCapabilities ht_ext_cap, TxBfCapability txbf_cap,
                       AselCapability asel_cap);
    static constexpr size_t kMinLen = 26;
    static constexpr size_t kMaxLen = 26;

    ElementHeader hdr;
    HtCapabilityInfo ht_cap_info;
    AmpduParams ampdu_params;
    SupportedMcsSet mcs_set;
    HtExtCapabilities ht_ext_cap;
    TxBfCapability txbf_cap;
    AselCapability asel_cap;

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.57
// Note this is a field within HtOperation element.
class HtOpInfoHead : public common::BitField<uint32_t> {
   public:
    constexpr explicit HtOpInfoHead(uint32_t op_info) : common::BitField<uint32_t>(op_info) {}
    constexpr HtOpInfoHead() = default;

    WLAN_BIT_FIELD(secondary_chan_offset, 0, 2);
    WLAN_BIT_FIELD(sta_chan_width, 2, 1);
    WLAN_BIT_FIELD(rifs_mode, 3, 1);
    WLAN_BIT_FIELD(reserved1, 4, 4);  // Note 802.11n D1.10 implementaions use these.

    WLAN_BIT_FIELD(ht_protect, 8, 2);
    WLAN_BIT_FIELD(nongreenfield_present, 10, 1);  // Nongreenfield HT STAs present.

    WLAN_BIT_FIELD(reserved2, 11, 1);          // Note 802.11n D1.10 implementaions use these.
    WLAN_BIT_FIELD(obss_non_ht, 12, 1);        // OBSS Non-HT STAs present.
    // IEEE 802.11-2016 Figure 9-339 has an incosistency so this is Fuchsia interpretation:
    // The channel number for the second segment in a 80+80 Mhz channel
    WLAN_BIT_FIELD(center_freq_seg2, 13, 8);   // VHT
    WLAN_BIT_FIELD(reserved3, 21, 3);

    WLAN_BIT_FIELD(reserved4, 24, 6);
    WLAN_BIT_FIELD(dual_beacon, 30, 1);
    WLAN_BIT_FIELD(dual_cts_protect, 31, 1);

    enum SecChanOffset {
        SECONDARY_NONE = 0,   // No secondary channel
        SECONDARY_ABOVE = 1,  // Secondary channel is above the primary channel
        RESERVED = 2,
        SECONDARY_BELOW = 3,  // Secondary channel is below the primary channel
    };

    enum StaChanWidth {
        TWENTY = 0,  // MHz
        ANY = 1,     // Any in the Supported Channel Width set
    };

    enum HtProtect {
        NONE = 0,
        NONMEMBER = 1,
        TWENTY_MHZ = 2,
        NON_HT_MIXED = 3,
    };
} __PACKED;

class HtOpInfoTail : public common::BitField<uint8_t> {
   public:
    constexpr explicit HtOpInfoTail(uint8_t val) : common::BitField<uint8_t>(val) {}
    constexpr HtOpInfoTail() = default;

    WLAN_BIT_FIELD(stbc_beacon, 0, 1);  // Add 32 for the original bit location.
    WLAN_BIT_FIELD(lsig_txop_protect, 1, 1);
    WLAN_BIT_FIELD(pco_active, 2, 1);
    WLAN_BIT_FIELD(pco_phase, 3, 1);
    WLAN_BIT_FIELD(reserved5, 4, 4);
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.57
struct HtOperation : public Element<HtOperation, element_id::kHtOperation> {
    static bool Create(void* buf, size_t len, size_t* actual, uint8_t primary_chan,
                       HtOpInfoHead head, HtOpInfoTail tail, SupportedMcsSet basic_mcs_set);
    static constexpr size_t kMinLen = 22;
    static constexpr size_t kMaxLen = 22;

    ElementHeader hdr;

    uint8_t primary_chan;  // Primary 20 MHz channel.

    // Implementation hack to support 40bits bitmap.
    HtOpInfoHead head;
    HtOpInfoTail tail;
    SupportedMcsSet basic_mcs_set;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.126
struct GcrGroupAddressElement {
    static bool Create(void* buf, size_t len, size_t* actual, const common::MacAddr& addr);
    static const size_t kMinLen = common::kMacAddrLen;
    static const size_t kMaxLen = common::kMacAddrLen;

    ElementHeader hdr;
    common::MacAddr gcr_group_addr;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.158.2
// Note this is a field of VhtCapabilities element
struct VhtCapabilitiesInfo : public common::BitField<uint32_t> {
   public:
    constexpr explicit VhtCapabilitiesInfo(uint32_t vht_cap_info)
        : common::BitField<uint32_t>(vht_cap_info) {}
    constexpr VhtCapabilitiesInfo() = default;

    WLAN_BIT_FIELD(max_mpdu_len, 0, 2);

    // Supported channel width set. See IEEE Std 80.211-2016, Table 9-250.
    WLAN_BIT_FIELD(supported_cbw_set, 2, 2);

    WLAN_BIT_FIELD(rx_ldpc, 4, 1);
    WLAN_BIT_FIELD(sgi_cbw80, 5, 1);   // CBW80 only
    WLAN_BIT_FIELD(sgi_cbw160, 6, 1);  // CBW160 and CBW80P80
    WLAN_BIT_FIELD(tx_stbc, 7, 1);
    WLAN_BIT_FIELD(rx_stbc, 8, 3);
    WLAN_BIT_FIELD(su_bfer, 11, 1);       // Single user beamformer capable
    WLAN_BIT_FIELD(su_bfee, 12, 1);       // Single user beamformee capable
    WLAN_BIT_FIELD(bfee_sts, 13, 3);      // Beamformee Space-time spreading
    WLAN_BIT_FIELD(num_sounding, 16, 3);  // number of sounding dimensions
    WLAN_BIT_FIELD(mu_bfer, 19, 1);       // Multi user beamformer capable
    WLAN_BIT_FIELD(mu_bfee, 20, 1);       // Multi user beamformee capable
    WLAN_BIT_FIELD(txop_ps, 21, 1);       // Txop power save mode
    WLAN_BIT_FIELD(htc_vht, 22, 1);
    WLAN_BIT_FIELD(max_ampdu_exp, 23, 3);
    WLAN_BIT_FIELD(link_adapt, 26, 2);  // VHT link adaptation capable
    WLAN_BIT_FIELD(rx_ant_pattern, 28, 1);
    WLAN_BIT_FIELD(tx_ant_pattern, 29, 1);

    // Extended number of spatial stream bandwidth supported
    // See IEEE Std 80.211-2016, Table 9-250.
    WLAN_BIT_FIELD(ext_nss_bw, 30, 2);

    enum MaxMpduLen {
        OCTETS_3895 = 0,
        OCTETS_7991 = 1,
        OCTETS_11454 = 2,
        // 3 reserved
    };

    enum VhtLinkAdaptation {
        LINK_ADAPT_NO_FEEDBACK = 0,
        // 1 reserved
        LINK_ADAPT_UNSOLICITED = 2,
        LINK_ADAPT_BOTH = 3,
    };

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.158.3
struct VhtMcsNss : public common::BitField<uint64_t> {
   public:
    constexpr explicit VhtMcsNss(uint64_t vht_mcs_nss) : common::BitField<uint64_t>(vht_mcs_nss) {}
    constexpr VhtMcsNss() = default;

    // Rx VHT-MCS Map
    WLAN_BIT_FIELD(rx_max_mcs_ss1, 0, 2);
    WLAN_BIT_FIELD(rx_max_mcs_ss2, 2, 2);
    WLAN_BIT_FIELD(rx_max_mcs_ss3, 4, 2);
    WLAN_BIT_FIELD(rx_max_mcs_ss4, 6, 2);
    WLAN_BIT_FIELD(rx_max_mcs_ss5, 8, 2);
    WLAN_BIT_FIELD(rx_max_mcs_ss6, 10, 2);
    WLAN_BIT_FIELD(rx_max_mcs_ss7, 12, 2);
    WLAN_BIT_FIELD(rx_max_mcs_ss8, 14, 2);

    WLAN_BIT_FIELD(rx_max_data_rate, 16, 13);
    WLAN_BIT_FIELD(max_nsts, 29, 3);

    // Tx VHT-MCS Map
    WLAN_BIT_FIELD(tx_max_mcs_ss1, 32, 2);
    WLAN_BIT_FIELD(tx_max_mcs_ss2, 34, 2);
    WLAN_BIT_FIELD(tx_max_mcs_ss3, 36, 2);
    WLAN_BIT_FIELD(tx_max_mcs_ss4, 38, 2);
    WLAN_BIT_FIELD(tx_max_mcs_ss5, 40, 2);
    WLAN_BIT_FIELD(tx_max_mcs_ss6, 42, 2);
    WLAN_BIT_FIELD(tx_max_mcs_ss7, 44, 2);
    WLAN_BIT_FIELD(tx_max_mcs_ss8, 46, 2);
    WLAN_BIT_FIELD(tx_max_data_rate, 48, 13);

    WLAN_BIT_FIELD(ext_nss_bw, 61, 1);
    // bit 62, 63 reserved

    enum VhtMcsSet {
        VHT_MCS_0_TO_7 = 0,
        VHT_MCS_0_TO_8 = 1,
        VHT_MCS_0_TO_9 = 2,
        VHT_MCS_NONE = 3,
    };

    uint8_t get_rx_max_mcs_ss(uint8_t ss_num) const {
        ZX_DEBUG_ASSERT(1 <= ss_num && ss_num <= 8);
        constexpr uint8_t kMcsBitOffset = 0;  // rx_max_mcs_ss1
        constexpr uint8_t kBitWidth = 2;
        uint8_t offset = kMcsBitOffset + (ss_num - 1) * kBitWidth;
        uint64_t mask = ((1ull << kBitWidth) - 1) << offset;
        return (val() & mask) >> offset;
    }

    uint8_t get_tx_max_mcs_ss(uint8_t ss_num) const {
        ZX_DEBUG_ASSERT(1 <= ss_num && ss_num <= 8);
        constexpr uint8_t kMcsBitOffset = 32;  // tx_max_mcs_ss1
        constexpr uint8_t kBitWidth = 2;
        uint8_t offset = kMcsBitOffset + (ss_num - 1) * kBitWidth;
        uint64_t mask = ((1ull << kBitWidth) - 1) << offset;
        return (val() & mask) >> offset;
    }
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.158
struct VhtCapabilities : public Element<VhtCapabilities, element_id::kVhtCapabilities> {
    static bool Create(void* buf, size_t len, size_t* actual,
                       const VhtCapabilitiesInfo& vht_cap_info, const VhtMcsNss& vht_mcs_nss);
    static constexpr size_t kMinLen = 12;
    static constexpr size_t kMaxLen = 12;

    ElementHeader hdr;

    VhtCapabilitiesInfo vht_cap_info;
    VhtMcsNss vht_mcs_nss;

} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.159
struct VhtOperation : public Element<VhtOperation, element_id::kVhtOperation> {
    static bool Create(void* buf, size_t len, size_t* actual, uint8_t vht_cbw,
                       uint8_t center_freq_seg0, uint8_t center_freq_seg1,
                       const VhtMcsNss& vht_mcs_nss);
    static constexpr size_t kMinLen = 5;
    static constexpr size_t kMaxLen = 5;

    ElementHeader hdr;

    uint8_t vht_cbw;
    uint8_t center_freq_seg0;
    uint8_t center_freq_seg1;

    VhtMcsNss vht_mcs_nss;

    enum VhtChannelBandwidth {
        VHT_CBW_20_40 = 0,
        VHT_CBW_80_160_80P80 = 1,
        VHT_CBW_160 = 2,    // Deprecated
        VHT_CBW_80P80 = 3,  // Deprecated

        // 4 - 255 reserved
    };

} __PACKED;
SupportedMcsSet IntersectMcs(const SupportedMcsSet& lhs, const SupportedMcsSet& rhs);
HtCapabilities IntersectHtCap(const HtCapabilities& lhs, const HtCapabilities& rhs);
VhtCapabilities IntersectVhtCap(const VhtCapabilities &lhs, const VhtCapabilities& rhs);
}  // namespace wlan
