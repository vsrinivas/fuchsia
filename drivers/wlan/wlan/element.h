// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/assert.h>
#include <magenta/compiler.h>
#include <magenta/types.h>
#include <fbl/type_support.h>

#include <drivers/wifi/common/bitfield.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace wlan {

// IEEE Std 802.11-2016, 9.4.2.1
struct ElementHeader {
    uint8_t id;
    uint8_t len;
} __PACKED;

template <typename E, uint8_t ID>
struct Element {
    static constexpr uint8_t element_id() { return ID; }
    size_t len() const {
        return sizeof(ElementHeader) + static_cast<E*>(this)->hdr.len;
    }
};

// An ElementReader can be used to retrieve Elements from a Management frame. The peek() method
// will read the ElementHeader without advancing the iterator. The caller may then use the id in the
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

    template <typename E>
    const E* read() {
        static_assert(fbl::is_base_of<Element<E, E::element_id()>, E>::value,
                      "Only Elements may be retrieved.");
        if (!is_valid()) return nullptr;
        if (offset_ + sizeof(E) > len_) return nullptr;
        const E* elt = reinterpret_cast<const E*>(buf_ + offset_);
        MX_DEBUG_ASSERT(elt->hdr.id == E::element_id());
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

    template <typename E, typename... Args>
    bool write(Args&&... args) {
        static_assert(fbl::is_base_of<Element<E, E::element_id()>, E>::value,
                      "Only Elements may be inserted.");
        if (offset_ >= len_) return false;

        size_t actual = 0;
        bool success =
            E::Create(buf_ + offset_, len_ - offset_, &actual, std::forward<Args>(args)...);
        if (!success) return false;
        offset_ += actual;
        MX_DEBUG_ASSERT(offset_ <= len_);
        return true;
    }

    size_t size() const { return offset_; }

  private:
    uint8_t* buf_;
    const size_t len_;
    size_t offset_ = 0;
};

// IEEE Std 802.11-2016, 9.4.2.1 Table 9-77
namespace element_id {
enum ElementId : uint8_t {
    kSsid = 0,
    kSuppRates = 1,
    // 2 Reserved
    kDsssParamSet = 3,
    kCfParamSet = 4,
    kTim = 5,
    kIbssParamSet = 6,
    kCountry = 7,
    // 8-9 Reserved
    kRequest = 10,
    kBssLoad = 11,
    kEdcaParamSet = 12,
    kTspec = 13,
    kTclas = 14,
    kSchedule = 15,
    kChallengeText = 16,
    // 17-31 Reserved
    kPowerConstraint = 32,
    kPowerCapability = 33,
    kTpcRequest = 34,
    kTpcReport = 35,
    kSupportedChannels = 36,
    kChannelSwitchAnn = 37,
    kMeasurementRequest = 38,
    kMeasurementReport = 39,
    kQuiet = 40,
    kIbssDfs = 41,
    kErp = 42,
    kTsDelay = 43,
    kTclasProcessing = 44,
    kHtCapabilities = 45,
    kQosCapability = 46,
    // 47 Reserved
    kRsn = 48,
    // 49 Reserved
    kExtSuppRates = 50,
    kApChannelReport = 51,
    kNeighborReport = 52,
    kRcpi = 53,
    kMobilityDomain = 54,
    kFastBssTransition = 55,
    kTimeoutInterval = 56,
    kRicData = 57,
    kDseRegisteredLocation = 58,
    kSuppOperatingClasses = 59,
    kExtChannelSwitchAnn = 60,
    kHtOperation = 61,
    kSecondaryChannelOffset = 62,
    kBssAvgAccessDelay = 63,
    kAntenna = 64,
    kRsni = 65,
    kMeasurementPilotTrans = 66,
    kBssAvailAdmissionCapacity = 67,
    kBssAcAccessDelay = 68,
    kTimeAdvertisement = 69,
    kRmEnabledCapabilities = 70,
    kMultipleBssid = 71,
    k2040BssCoex = 72,
    k2040BssIntolerantChanReport = 73,
    kOverlappingBssScanParams = 74,
    kRicDescriptor = 75,
    kManagementMic = 76,
    // 77 not defined
    kEventRequest = 78,
    kEventReport = 79,
    kDiagnosticRequest = 80,
    kDiagnosticReport = 81,
    kLocationParams = 82,
    kNontransmittedBssidCapability = 83,
    kSsidList = 84,
    kMultipleBssidIndex = 85,
    kFmsDescriptor = 86,
    kFmsRequest = 87,
    kFmsResponse = 88,
    kQosTrafficCapability = 89,
    kBssMaxIdlePeriod = 90,
    kTfsRequest = 91,
    kTfsResponse = 92,
    kWnmSleepMode = 93,
    kTimBroadcastRequest = 94,
    kTimBroadcastResponse = 95,
    kCollocatedInterferenceReport = 96,
    kChannelUsage = 97,
    kTimeZone = 98,
    kDmsRequest = 99,
    kDmsResponse = 100,
    kLinkIdentifier = 101,
    kWakeupSchedule = 102,
    // 103 not defined
    kChannelSwitchTiming = 104,
    kPtiControl = 105,
    kTpuBufferStatus = 106,
    kInterworking = 107,
    kAdvertisementProtocol = 108,
    kExpeditedBandwidthRequest = 109,
    kQosMap = 110,
    kRoamingConsortium = 111,
    kEmergencyAlertId = 112,
    kMeshConfiguration = 113,
    kMeshId = 114,
    kMeshLinkMetricReport = 115,
    kCongestionNotification = 116,
    kMeshPeeringManagement = 117,
    kMeshChannelSwitchParams = 118,
    kMeshAwakeWindow = 119,
    kBeaconTiming = 120,
    kMccaopSetupRequest = 121,
    kMccaopSetupReply = 122,
    kMccaopAdvertisement = 123,
    kMccaopTeardown = 124,
    kGann = 125,
    kRann = 126,
    kExtCapabilities = 127,
    // 128-129 Reserved
    kPreq = 130,
    kPrep = 131,
    kPerr = 132,
    // 133-136 Reserved
    kPxu = 137,
    kPxuc = 138,
    kAuthenticatedMeshPeeringExchg = 139,
    kMic = 140,
    kDestinationUri = 141,
    kUapsdCoex = 142,
    kDmgWakeupSchedule = 143,
    kExtSchedule = 144,
    kStaAvailability = 145,
    kDmgTspec = 146,
    kNextDmgAti = 147,
    kDmgCapabilities = 148,
    // 149-150 Reserved
    kDmgOperation = 151,
    kDmgBssParamChange = 152,
    kDmgBeamRefinement = 153,
    kChannelMeasurementFeedback = 154,
    // 155-156 Reserved
    kAwakeWindow = 157,
    kMultiband = 158,
    kAddbaExtension = 159,
    kNextPcpList = 160,
    kPcpHandover = 161,
    kDmgLinkMargin = 162,
    kSwitchingStream = 163,
    kSessionTransition = 164,
    kDynamicTonePairingReport = 165,
    kClusterReport = 166,
    kRelayCapabilities = 167,
    kRelayTransferParamSet = 168,
    kBeamLinkMaintenance = 169,
    kMultipleMacSublayers = 170,
    kUpid = 171,
    kDmgLinkAdaptationAck = 172,
    // 173 Reserved
    kMccaopAdvertisementOverview = 174,
    kQuietPeriodRequest = 175,
    // 176 Reserved
    kQuietPeriodResponse = 177,
    // 178-180 Reserved
    kQmfPolicy = 181,
    kEcapcPolicy = 182,
    kClusterTimeOffset = 183,
    kIntraAccessCategoryPriority = 184,
    kScsDescriptor = 185,
    kQloadReport = 186,
    kHccaTxopUpdateCount = 187,
    kHigherLayerStreamId = 188,
    kGcrGroupAddres = 189,
    kAntennaSectorIdPattern = 190,
    kVhtCapabilities = 191,
    kVhtOperation = 192,
    kExtBssLoad = 193,
    kWideBandwidthChannelSwitch = 194,
    kTransmitPowerEnvelope = 195,
    kChannelSwitchWrapper = 196,
    kAid = 197,
    kQuietChannel = 198,
    kOperatingModeNotification = 199,
    kUpsim = 200,
    kReducedNeighborReport = 201,
    kTvhtOperation = 202,
    // 203 Reserved
    kDeviceLocation = 204,
    kWhiteSpaceMap = 205,
    kFineTimingMeasurementParams = 206,
    // 207-220 Reserved
    kVendorSpecific = 221,
    // 222-254 Reserved
    kElementWithExtension = 255,
};
}  // namespace element_id

enum ElementIdExtension : uint8_t {
    // 0-8 Reserved
    kFtmSynchronizationInformation = 9,
    kExtRequest = 10,
    kEstimatedServiceParams = 11,
    // 12-13 not defined
    kFutureChannelGuidance = 14,
    // 15-255 Reserved
};

struct SsidElement : public Element<SsidElement, element_id::kSsid> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, const char* ssid);
    static const size_t kMaxLen = 32;

    ElementHeader hdr;
    char ssid[];
} __PACKED;

struct SupportedRatesElement : public Element<SupportedRatesElement, element_id::kSuppRates> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual,
                       const std::vector<uint8_t>& rates);
    static const size_t kMaxLen = 8;

    ElementHeader hdr;
    uint8_t rates[];
} __PACKED;

struct DsssParamSetElement : public Element<DsssParamSetElement, element_id::kDsssParamSet> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, uint8_t chan);

    ElementHeader hdr;
    uint8_t current_chan;
} __PACKED;

struct CfParamSetElement : public Element<CfParamSetElement, element_id::kCfParamSet> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, uint8_t count, uint8_t period,
                       uint16_t max_duration, uint16_t dur_remaining);

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
    static bool Create(uint8_t* buf, size_t len, size_t* actual, uint8_t dtim_count,
                       uint8_t dtim_period, BitmapControl bmp_ctrl,
                       const std::vector<uint8_t>& bmp);
    static const size_t kMaxLen = 251;

    ElementHeader hdr;
    uint8_t dtim_count;
    uint8_t dtim_period;
    BitmapControl bmp_ctrl;
    uint8_t bmp[];

    bool traffic_buffered(uint16_t aid) const;
} __PACKED;

struct CountryElement : public Element<CountryElement, element_id::kCountry> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, const char* country);

    ElementHeader hdr;
    char country[3];
    uint8_t triplets[];  // TODO(tkilbourn): define these
} __PACKED;

struct ExtendedSupportedRatesElement :
    public Element<ExtendedSupportedRatesElement, element_id::kExtSuppRates> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual,
                       const std::vector<uint8_t>& rates);
    static const size_t kMaxLen = 255;

    ElementHeader hdr;
    uint8_t rates[];
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-131
namespace cipher_suite_type {
enum CipherSuiteType : uint8_t {
    kGroupCipherSuite = 0,
    kWep40 = 1,
    kTkip = 2,
    // 3 Reserved
    kCcmp128 = 4,
    kWep104 = 5,
    kBipCmac128 = 6,
    kGroupAddressedTrafficForbidden = 7,
    kGcmp128 = 8,
    kGcmp256 = 9,
    kCcmp256 = 10,
    kBipGmac128 = 11,
    kBipGmac256 = 12,
    kBipCmac256 = 13,
    // 14 - 255 Reserved
};
} // namespace cipher_suite_type

// IEEE Std 802.11-2016, 9.4.2.25.2, Table 9-133
namespace akm_suite_type {
enum AkmSuiteType : uint8_t {
    // 0 Reserved
    k8021X_Pmksa = 1,
    kPsk = 2,
    k8021X_Ft = 3,
    kPsk_Ft = 4,
    k8021X_Pmksa_Sha256 = 5,
    kPsk_Sha256 = 6,
    kTdls = 7,
    kSae = 8,
    kSae_Ft = 9,
    kApPeerKey = 10,
    k8021X_Sha256 = 11,
    k8021X_Sha384 = 12,
    k8021X_Ft_Sha384 = 13,
    // 14 - 255 Reserved
};
} // namespace cipher_suite_type

const uint8_t kRsneSuiteOui[3] = { 0x00, 0x0F, 0xAC };
const uint8_t kRsneVersion = 1;
const uint16_t kEapolProtocolId = 0x888E;

// IEEE Std 802.11-2016, 9.4.2.25.4, Figure 9-257
class RsnCapabilities : public common::BitField<uint16_t> {
  public:
    WLAN_BIT_FIELD(preauthentication, 0, 1);
    WLAN_BIT_FIELD(no_pairwise, 1, 1);
    WLAN_BIT_FIELD(ptk_replay_counter, 2, 2);
    WLAN_BIT_FIELD(gtksa_replay_counter, 4, 2);
    WLAN_BIT_FIELD(mfpr, 6, 1);
    WLAN_BIT_FIELD(mfpc, 7, 1);
    WLAN_BIT_FIELD(joint_multiband_rsna, 8, 1);
    WLAN_BIT_FIELD(peer_key_enabled, 9, 1);
    WLAN_BIT_FIELD(spp_a_msdu_capable, 10, 1);
    WLAN_BIT_FIELD(spp_a_msdu_required, 11, 1);
    WLAN_BIT_FIELD(pbac, 12, 1);
    WLAN_BIT_FIELD(ex_key_id_ind_addr_frames, 13, 1);
    // 2-bit Reserved
};

// IEEE Std 802.11-2016, 9.4.2.25.2
struct CipherSuite {
    uint8_t oui[3];
    cipher_suite_type::CipherSuiteType type;
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.25.3
struct AkmSuite {
    uint8_t oui[3];
    akm_suite_type::AkmSuiteType type;
} __PACKED;

template <typename T>
struct RsnOptionalList {
    uint16_t count;
    T list[];

    size_t size() { return sizeof(RsnOptionalList) + count * sizeof(T); }
} __PACKED;

// IEEE Std 802.11-2016, 9.4.2.25.1
struct RsnElement : public Element<RsnElement, element_id::kRsn> {
    static bool Create(uint8_t* buf, size_t len, size_t* actual, uint16_t version,
                     CipherSuite* group_data_cipher_suite = nullptr,
                     const std::vector<CipherSuite>& pairwise_cipher_suite_list = {},
                     const std::vector<AkmSuite>& akm_suite_list = {},
                     RsnCapabilities* rsn_cap = nullptr,
                     const std::vector<__uint128_t>& pmkid_list = {},
                     CipherSuite* group_mgmt_cipher_suite = nullptr);
    static const size_t kMaxLen = 255;

    ElementHeader hdr;
    uint16_t version;
    uint8_t fields[];

    // All following fields are optional. If one optional field is absent, none of the subsequent
    // ones is included.

    CipherSuite* group_data_cipher_suite() const {
        return next_optional<CipherSuite>(fields, 0);
    }

    RsnOptionalList<CipherSuite>* pairwise_cipher_suite() const {
        auto prev = group_data_cipher_suite();
        if (!prev) return nullptr;
        return next_optional<RsnOptionalList<CipherSuite>>(prev, sizeof(CipherSuite));
    }

    RsnOptionalList<AkmSuite>* akm_suite() const {
        auto prev = pairwise_cipher_suite();
        if (!prev) return nullptr;
        return next_optional<RsnOptionalList<AkmSuite>>(prev, prev->size());
    }

    RsnCapabilities* rsn_cap() const {
        auto prev = akm_suite();
        if (!prev) return nullptr;
        return next_optional<RsnCapabilities>(prev, prev->size());
    }

    RsnOptionalList<__uint128_t>* pmkid() const {
        auto prev = rsn_cap();
        if (!prev) return nullptr;
        return next_optional<RsnOptionalList<__uint128_t>>(prev, sizeof(RsnCapabilities));
    }

    CipherSuite* group_mgmt_cipher_suite() const {
        auto prev = pmkid();
        if (!prev) return nullptr;
        return next_optional<CipherSuite>(prev, prev->size());
    }

  private:
    template <typename T, typename U>
    T* next_optional(const U* previous, size_t len) const;
} __PACKED;

}  // namespace wlan
