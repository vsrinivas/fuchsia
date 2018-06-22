// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <endian.h>
#include <fbl/algorithm.h>
#include <fbl/type_support.h>
#include <lib/zx/time.h>
#include <wlan/common/action_frame.h>
#include <wlan/common/bitfield.h>
#include <wlan/common/element.h>
#include <wlan/common/macaddr.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <cstdint>

namespace wlan {

// wlan_tu_t is an 802.11 Time Unit.
typedef uint64_t wlan_tu_t;

// One Time Unit is equal to 1024 microseconds.
static constexpr zx::duration TimeUnit = zx::usec(1024);

// Converts a WLAN TU to zx::duration.
//
// This is a template because it allows us to guard against some implicit conversion.  For example,
// if this were a regular function accepting a wlan_tu_t, the following code would compile:
//
//     foo(WLAN_TU(-1));
//
template <typename T> static inline constexpr zx::duration WLAN_TU(T n) {
    static_assert(fbl::is_unsigned_integer<T>::value, "Time unit must be an unsigned integer");
    return TimeUnit * n;
}

// Frame types and subtypes
// IEEE Std 802.11-2016, 9.2.4.1.3

enum FrameType : uint8_t {
    kManagement = 0x00,
    kControl = 0x01,
    kData = 0x02,  // TODO(porce): Distinguish from DataSubtype's kData by enum class.
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

// IEEE Std 802.11-2016, 9.2.4.1.3, Table 9-1
enum DataSubtypeBitmask : uint8_t {
    // This leads to enum DataSubtype
    kBitmaskCfAck = (1 << 0),   // Bit 4
    kBitmaskCfPoll = (1 << 1),  // Bit 5
    kBitmaskNull = (1 << 2),    // Bit 6. If not Null, it means Data.
    kBitmaskQos = (1 << 3),     // Bit 7
};

// IEEE Std 802.11-2016, 9.2.4.1.3, Table 9-1
enum DataSubtype : uint8_t {
    kDataSubtype = 0x00,  // TODO(porce): Avoid namespace collision with enum FrameType's kData.
    kDataCfack = 0x01,
    kDataCfpoll = 0x02,
    kDataCfackCfpoll = 0x03,
    kNull = 0x04,
    kCfack = 0x05,
    kCfpoll = 0x06,
    kCfackCfpoll = 0x07,
    kQosdata = 0x08,
    kQosdataCfack = 0x09,
    kQosdataCfpoll = 0x0a,
    kQosdataCfackCfpoll = 0x0b,
    kQosnull = 0x0c,
    // Reserved 0x0d,
    kQosCfpoll = 0x0e,
    kQosCfackCfpoll = 0x0f,
};

// IEEE Std 802.11-2016, 9.2.4.4
class SequenceControl : public common::BitField<uint16_t> {
   public:
    WLAN_BIT_FIELD(frag, 0, 4);
    WLAN_BIT_FIELD(seq, 4, 12);
};

constexpr uint16_t kMaxSequenceNumber = (1 << 12) - 1;

// IEEE Std 802.11-2016, 9.2.4.5.4, Table 9-9
namespace ack_policy {
enum AckPolicy : uint8_t {
    kNormalAck = 0,  // or Implicit Block Ack Request
    kNoAck = 1,
    kNoExplicitAck = 2,  // or PSMP Ack
    kBlockAck = 3,
};

}  // namespace ack_policy

// IEEE Std 802.11-2016, 9.2.4.5.1, Table 9-6
class QosControl : public common::BitField<uint16_t> {
   public:
    WLAN_BIT_FIELD(tid, 0, 4);
    WLAN_BIT_FIELD(eosp, 4, 1);  // End of Service Period
    WLAN_BIT_FIELD(ack_policy, 5, 2);
    WLAN_BIT_FIELD(amsdu_present, 7, 1);

    // Interpretation varies
    WLAN_BIT_FIELD(byte, 8, 8);
};

// IEEE Std 802.11-2016, 9.2.4.6
class HtControl : public common::BitField<uint32_t> {
   public:
    WLAN_BIT_FIELD(vht, 0, 1);

    // Structure of this middle section is defined in 9.2.4.6.2 for HT,
    // and 9.2.4.6.3 for VHT.
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

// TODO: Replace native ReasonCode with FIDL ReasonCode
// IEEE Std 802.11-2016, 9.4.1.7, Table 9-45
namespace reason_code {
enum ReasonCode : uint16_t {
    // 0 Reserved
    kUnspecifiedReason = 1,
    kInvalidAuthentication = 2,
    kLeavingNetworkDeauth = 3,
    kReasonInactivity = 4,
    kNoMoreStas = 5,
    kInvalidClass2Frame = 6,
    kInvalidClass3Frame = 7,
    kLeavingNetworkDisassoc = 8,
    kNotAuthenticated = 9,
    kUnacceptablePowerCapability = 10,
    kUnacceptableSupportedChannels = 11,
    kBssTransitionDisassoc = 12,
    kReasonInvalidElement = 13,
    kMicFailure = 14,
    k4WayHandshakeTimeout = 15,
    kGkHandshakeTimeout = 16,
    kHandshakeElementMismatch = 17,
    kReasonInvalidGroupCipher = 18,
    kReasonInvalidPairwiseCipher = 19,
    kReasonInvalidAkmp = 20,
    kUnsupportedRsneVersion = 21,
    kInvalidRsneCapabilities = 22,
    k8021XAuthFailed = 23,
    kReasonCipherOutOfPolicy = 24,
    kTdlsPeerUnreachable = 25,
    kTdlsUnspecifiedReason = 26,
    kSspRequestedDisassoc = 27,
    kNoSspRoamingAgreement = 28,
    kBadCipherOrAkm = 29,
    kNotAuthorizedThisLocation = 30,
    kServiceChangePrecludesTs = 31,
    kUnspecifiedQosReason = 32,
    kNotEnoughBandwidth = 33,
    kMissingAcks = 34,
    kExceededTxop = 35,
    kStaLeaving = 36,
    // The following groups of reasons share the same code
    kEndTs = 37,
    kEndBa = 37,
    kEndDls = 37,
    kUnknownTs = 38,
    kUnknownBa = 38,
    kTimeout = 39,
    // 40-44 Reserved
    kPeerkeyMismatch = 45,
    kPeerInitiated = 46,
    kApInitiated = 47,
    kReasonInvalidFtActionFrameCount = 48,
    kReasonInvalidPmkid = 49,
    kReasonInvalidMde = 50,
    kReasonInvalidFte = 51,
    kMeshPeeringCanceled = 52,
    kMeshMaxPeers = 53,
    kMeshConfigurationPolicyViolation = 54,
    kMeshCloseRcvd = 55,
    kMeshMaxRetries = 56,
    kMeshConfirmTimeout = 57,
    kMeshInvalidGtk = 58,
    kMeshInconsistentParameters = 59,
    kMeshInvalidSecurityCapability = 60,
    kMeshPathErrorNoProxyInformation = 61,
    kMeshPathErrorNoForwardingInformation = 62,
    kMeshPathErrorDestinationUnreachable = 63,
    kMacAddressAlreadyExistsInMbss = 64,
    kMeshChannelSwitchRegulatoryRequirements = 65,
    kMeshChannelSwitchUnspecified = 66,
    // 67 - 65535 Reserved
};
}  // namespace reason_code

// IEEE Std 802.11-2016, 9.4.1.9, Table 9-46
namespace status_code {
enum StatusCode : uint16_t {
    kSuccess = 0,
    kRefused = 1,
    kRefusedReasonUnspecified = 1,
    kTdlsRejectedAlternativeProvided = 2,
    kTdlsRejected = 3,
    // 4 Reserved
    kSecurityDisabled = 5,
    kUnacceptableLifetime = 6,
    kNotInSameBss = 7,
    // 8-9 Reserved
    kRefusedCapabilitiesMismatch = 10,
    kDeniedNoAssociationExists = 11,
    kDeniedOtherReason = 12,
    kUnsupportedAuthAlgorithm = 13,
    kTransactionSequenceError = 14,
    kChallengeFailure = 15,
    kRejectedSequenceTimeout = 16,
    kDeniedNoMoreStas = 17,
    kRefusedBasicRatesMismatch = 18,
    kDeniedNoShortPreambleSupport = 19,
    // 20-21 Reserved
    kRejectedSpectrumManagementRequired = 22,
    kRejectedBadPowerCapability = 23,
    kRejectedBadSupportedChannels = 24,
    kDeniedNoShortSlotTimeSupport = 25,
    // 26 Reserved
    kDeniedNoHtSupport = 27,
    kR0khUnreachable = 28,
    kDeniedPcoTimeNotSupported = 29,
    kRefusedTemporarily = 30,
    kRobustManagementPolicyViolation = 31,
    kUnspecifiedQosFailure = 32,
    kDeniedInsufficientBandwidth = 33,
    kDeniedPoorChannelConditions = 34,
    kDeniedQosNotSupported = 35,
    // 36 Reserved
    kRequestDeclined = 37,
    kInvalidParameters = 38,
    kRejectedWithSuggestedChanges = 39,
    kStatusInvalidElement = 40,
    kStatusInvalidGroupCipher = 41,
    kStatusInvalidPairwiseCipher = 42,
    kStatusInvalidAkmp = 43,
    kUnsupportedRsneVersion = 44,
    kInvalidRsneCapabilities = 45,
    kStatusCipherOutOfPolicy = 46,
    kRejectedForDelayPeriod = 47,
    kDlsNotAllowed = 48,
    kNotPresent = 49,
    kNotQosSta = 50,
    kDeniedListenIntervalTooLarge = 51,
    kStatusInvalidFtActionFrameCount = 52,
    kStatusInvalidPmkid = 53,
    kStatusInvalidMde = 54,
    kStatusInvalidFte = 55,
    kRequestedTclasNotSupported_56 = 56,  // see kRequestedTclasNotSupported_80 below
    kInsufficientTclasProcessingResources = 57,
    kTryAnotherBss = 58,
    kGasAdvertisementProtocolNotSupported = 59,
    kNoOutstandingGasRequest = 60,
    kGasResponseNotReceivedFromServer = 61,
    kGasQueryTimeout = 62,
    kGasQueryResponseTooLarge = 63,
    kRejectedHomeWithSuggestedChanges = 64,
    kServerUnreachable = 65,
    // 66 Reserved
    kRejectedForSspPermissions = 67,
    kRefusedUnauthenticatedAccessNotSupported = 68,
    // 69-71 Reserved
    kInvalidRsne = 72,
    kUApsdCoexistanceNotSupported = 73,
    kUApsdCoexModeNotSupported = 74,
    kBadIntervalWithUApsdCoex = 75,
    kAntiCloggingTokenRequired = 76,
    kUnsupportedFiniteCyclicGroup = 77,
    kCannotFindAlternativeTbtt = 78,
    kTransmissionFailure = 79,
    kRequestedTclasNotSupported_80 = 80,  // see kRequestedTclasNotSupported_56 above
    kTclasResourcesExhausted = 81,
    kRejectedWithSuggestedBssTransition = 82,
    kRejectWithSchedule = 83,
    kRejectNoWakeupSpecified = 84,
    kSuccessPowerSaveMode = 85,
    kPendingAdmittingFstSession = 86,
    kPerformingFstNow = 87,
    kPendingGapInBaWindow = 88,
    kRejectUPidSetting = 89,
    // 90-91 Reserved
    kRefusedExternalReason = 92,
    kRefusedApOutOfMemory = 93,
    kRejectedEmergencyServicesNotSupported = 94,
    kQueryResponseOutstanding = 95,
    kRejectDseBand = 96,
    kTclasProcessingTerminated = 97,
    kTsScheduleConflict = 98,
    kDeniedWithSuggestedBandAndChannel = 99,
    kMccaopReservationConflict = 100,
    kMafLimitExceeded = 101,
    kMccaTrackLimitExceeded = 102,
    kDeniedDueToSpectrumManagement = 103,
    kDeniedVhtNotSupported = 104,
    kEnablementDenied = 105,
    kRestrictionFromAuthorizedGdb = 106,
    kAuthorizationDeenabled = 107,
    // 108-65535 Reserved
};
}  // namespace status_code

// IEEE Std 802.11-2016 9.2.3
// Length of optional fields
const uint16_t kHtCtrlLen = 4;
const uint16_t kQosCtrlLen = 2;
const uint16_t kFcsLen = 4;

// IEEE Std 802.11-2016, 9.2.4.1.1
class FrameControl : public common::BitField<uint16_t> {
   public:
    constexpr explicit FrameControl(uint16_t fc) : BitField<uint16_t>(fc) {}
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

    bool IsMgmt() const { return type() == FrameType::kManagement; }
    bool IsCtrl() const { return type() == FrameType::kControl; }
    bool IsData() const { return type() == FrameType::kData; }

    bool HasHtCtrl() const { return htc_order() != 0; }

    constexpr uint16_t len() const { return sizeof(FrameControl); }
};

// IEEE Std 802.11-2016, 9.3.3.2
struct MgmtFrameHeader {
    FrameControl fc;
    uint16_t duration;
    common::MacAddr addr1;
    common::MacAddr addr2;
    common::MacAddr addr3;
    SequenceControl sc;

    // Use accessors for optional field.
    // uint8_t ht_ctrl[4];

    uint16_t len() const {
        uint16_t len = sizeof(MgmtFrameHeader);
        if (fc.HasHtCtrl()) { len += kHtCtrlLen; }
        return len;
    }

    const HtControl* ht_ctrl() const {
        if (!fc.HasHtCtrl()) return nullptr;
        uint16_t offset = sizeof(MgmtFrameHeader);
        return reinterpret_cast<const HtControl* const>(raw() + offset);
    }

    HtControl* ht_ctrl() { return const_cast<HtControl*>(const_this()->ht_ctrl()); }

    bool IsBeacon() const { return fc.subtype() == ManagementSubtype::kBeacon; }

    bool IsProbeResponse() const { return fc.subtype() == ManagementSubtype::kProbeResponse; }

    bool IsAction() const { return fc.subtype() == ManagementSubtype::kAction; }

   private:
    const uint8_t* raw() const { return reinterpret_cast<const uint8_t*>(this); }
    const MgmtFrameHeader* const_this() { return const_cast<const MgmtFrameHeader*>(this); }

} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.3
struct Beacon {
    static constexpr ManagementSubtype Subtype() { return ManagementSubtype::kBeacon; }

    bool Validate(size_t len);

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
    static constexpr ManagementSubtype Subtype() { return ManagementSubtype::kProbeRequest; }

    bool Validate(size_t len);

    size_t hdr_len() { return 0; }
    uint8_t elements[];
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.11
struct ProbeResponse {
    static constexpr ManagementSubtype Subtype() { return ManagementSubtype::kProbeResponse; }

    bool Validate(size_t len);

    // 9.4.1.10
    uint64_t timestamp;
    // 9.4.1.3
    uint16_t beacon_interval;
    // 9.4.1.4
    CapabilityInfo cap;

    uint8_t elements[];
} __PACKED;

// IEEE Std 802.11-2016, 9.4.1.1
enum AuthAlgorithm : uint16_t {
    kOpenSystem = 0,
    kSharedKey = 1,
    kFastBssTransition = 2,
    kSae = 3,
    // 4-65534 Reserved
    kVendorSpecific = 65535,
};

// IEEE Std 802.11-2016, 9.3.3.12
struct Authentication {
    static constexpr ManagementSubtype Subtype() { return ManagementSubtype::kAuthentication; }

    // TODO(tkilbourn): bool Validate(size_t len)
    // Authentication frames are complicated, so when we need more than Open
    // System auth, figure out how to proceed.

    // 9.4.1.1
    uint16_t auth_algorithm_number;
    // 9.4.1.2
    uint16_t auth_txn_seq_number;
    // 9.4.1.9
    uint16_t status_code;

    uint8_t elements[];
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.13
struct Deauthentication {
    static constexpr ManagementSubtype Subtype() { return ManagementSubtype::kDeauthentication; }

    // 9.4.1.7
    uint16_t reason_code;

    // Vendor-specific elements and optional Management MIC element (MME) at the
    // end
    uint8_t elements[];
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.6
struct AssociationRequest {
    static constexpr ManagementSubtype Subtype() { return ManagementSubtype::kAssociationRequest; }

    bool Validate(size_t len);

    // 9.4.1.4
    CapabilityInfo cap;
    // 9.4.1.6
    uint16_t listen_interval;

    uint8_t elements[];
} __PACKED;

constexpr uint16_t kAidMask = (1 << 11) - 1;

// IEEE Std 802.11-2016, 9.3.3.7
struct AssociationResponse {
    static constexpr ManagementSubtype Subtype() { return ManagementSubtype::kAssociationResponse; }

    bool Validate(size_t len);

    // 9.4.1.4
    CapabilityInfo cap;
    // 9.4.1.9
    uint16_t status_code;
    // 9.4.1.8
    uint16_t aid;

    uint8_t elements[];
} __PACKED;

// IEEE Std 802.11-2016, 9.3.3.5
struct Disassociation {
    static constexpr ManagementSubtype Subtype() { return ManagementSubtype::kDisassociation; }

    // 9.4.1.7
    uint16_t reason_code;

    // Vendor-specific elements and optional Management MIC element (MME) at the
    // end
    uint8_t elements[];
} __PACKED;

// IEEE Std 802.11-2016, 9.3.2.1
struct DataFrameHeader {
    FrameControl fc;
    uint16_t duration;
    common::MacAddr addr1;
    common::MacAddr addr2;
    common::MacAddr addr3;
    SequenceControl sc;

    // Use accessors for optional fields.
    // MacAddr addr4;
    // uint8_t qos_ctrl[2];
    // HtControl* ht_ctrl;

    bool HasAddr4() const { return fc.to_ds() && fc.from_ds(); }
    bool HasQosCtrl() const {
        // Warning: Following bitmasking includes subtype 0x0d - Reserved.
        return (0 != (fc.subtype() & DataSubtypeBitmask::kBitmaskQos));
    }

    uint16_t len() const {
        uint16_t hdr_len = sizeof(DataFrameHeader);
        if (HasAddr4()) hdr_len += common::kMacAddrLen;
        if (HasQosCtrl()) hdr_len += kQosCtrlLen;
        if (fc.HasHtCtrl()) hdr_len += kHtCtrlLen;

        return hdr_len;
    }

    const common::MacAddr* addr4() const {
        if (!HasAddr4()) return nullptr;
        uint16_t offset = sizeof(DataFrameHeader);
        return reinterpret_cast<const common::MacAddr*>(raw() + offset);
    }

    common::MacAddr* addr4() { return const_cast<common::MacAddr*>(const_this()->addr4()); }

    const QosControl* qos_ctrl() const {
        if (!HasQosCtrl()) return nullptr;
        uint16_t offset = sizeof(DataFrameHeader);
        if (HasAddr4()) { offset += common::kMacAddrLen; }
        return reinterpret_cast<const QosControl*>(raw() + offset);
    }

    QosControl* qos_ctrl() { return const_cast<QosControl*>(const_this()->qos_ctrl()); }

    const HtControl* ht_ctrl() const {
        if (!fc.HasHtCtrl()) return nullptr;
        uint16_t offset = sizeof(DataFrameHeader);
        if (HasAddr4()) { offset += common::kMacAddrLen; }
        if (HasQosCtrl()) { offset += kQosCtrlLen; }
        return reinterpret_cast<const HtControl*>(raw() + offset);
    }

    HtControl* ht_ctrl() { return const_cast<HtControl*>(const_this()->ht_ctrl()); }

   private:
    const uint8_t* raw() const { return reinterpret_cast<const uint8_t*>(this); }
    const DataFrameHeader* const_this() { return const_cast<const DataFrameHeader*>(this); }
} __PACKED;

// IEEE Std 802.11-2016, 9.3.1.1
// Officially control frames share no common header .
class CtrlFrameIdentifier {
} __PACKED;

// IEEE Std 802.11-2016, 9.3.1.5
struct PsPollFrame : CtrlFrameIdentifier {
    FrameControl fc;
    uint16_t aid;
    common::MacAddr bssid;
    common::MacAddr ta;

    constexpr uint16_t len() const { return sizeof(FrameControl); }
} __PACKED;

// IEEE Std 802.2, 1998 Edition, 3.2
// IETF RFC 1042
struct LlcHeader {
    uint8_t dsap;
    uint8_t ssap;
    uint8_t control;
    uint8_t oui[3];
    uint16_t protocol_id;
    uint8_t payload[];
} __PACKED;

// IEEE Std 802.11-2016, 9.3.2.2.2
struct AmsduSubframeHeader {
    // Note this is the same as the IEEE 802.3 frame format.
    common::MacAddr da;
    common::MacAddr sa;
    uint16_t msdu_len_be;  // Stored in network byte order. Use accessors.

    uint16_t msdu_len() const { return be16toh(msdu_len_be); }

    void set_msdu_len(uint16_t msdu_len) { msdu_len_be = htobe16(msdu_len); }
} __PACKED;

// IEEE Std 802.11-2016, 9.3.2.2.3
// Non-DMG stations do not transmit in this form.
struct AmsduSubframeHeaderShort {
    uint16_t msdu_len_be;
} __PACKED;

struct AmsduSubframe {
    AmsduSubframeHeader hdr;
    uint8_t msdu[];
    // msdu[] is followed by conditional zero-padding for alignment of the amsdu subframe by 4
    // bytes.

    const LlcHeader* get_msdu() const { return reinterpret_cast<const LlcHeader*>(msdu); }

    size_t PaddingLen(bool is_last_subframe) const {
        size_t base_len = sizeof(AmsduSubframeHeader) + hdr.msdu_len();
        return (is_last_subframe) ? 0 : (fbl::round_up(base_len, 4u) - base_len);
    }
} __PACKED;

// RFC 1042
constexpr uint8_t kLlcSnapExtension = 0xaa;
constexpr uint8_t kLlcUnnumberedInformation = 0x03;
const uint8_t kLlcOui[3] = {};

constexpr size_t kDataFrameHdrLenMax =
    sizeof(DataFrameHeader) + common::kMacAddrLen + kQosCtrlLen + kHtCtrlLen;

// IEEE Std 802.11-2016, 9.2.3
struct FrameHeader {
    // Common fields for Mgmt, Control, Data frames
    FrameControl fc;
    uint16_t duration;
    common::MacAddr addr1;

    // Common fields for Mgmt, Data frames as well as non CTS/ACK Control frames
    common::MacAddr addr2;

    // Common fields for Mgmt, Data frames
    common::MacAddr addr3;
    SequenceControl sc;

    uint16_t len() const {
        if (fc.IsData()) { return reinterpret_cast<const DataFrameHeader*>(this)->len(); }
        if (fc.IsMgmt()) { return reinterpret_cast<const MgmtFrameHeader*>(this)->len(); }
        if (fc.IsCtrl()) {
            if (fc.subtype() < ControlSubtype::kBeamformingReportPoll) {
                // Reserved and unspecified.
                // This is likely to be a wrongful casting onto a byte stream.
                return 0;
            }
            if (fc.subtype() == ControlSubtype::kCts || fc.subtype() == ControlSubtype::kAck) {
                return 10;  // FC(2) + Duration/ID(2) + RA(6)
            } else {
                return 16;  // FC(2) + Duration/ID(2) + RA(6) + TA(6)
            }
        }

        // Extension. Unspecified.
        // This is likely to be a wrongful casting onto a byte stream.
        return 0;
    }

    // TODO(porce): Frame caster
    // eg. ManagementFrameHeader* AsMgmtFrameHdr();
    // eg. DataFrameHeader* AsDataFrameHdr();

} __PACKED;

// IEEE Std 802.3-2015, 3.1.1
struct EthernetII {
    common::MacAddr dest;
    common::MacAddr src;
    uint16_t ether_type;
    uint8_t payload[];

    constexpr uint16_t len() const { return sizeof(EthernetII); }
} __PACKED;

// IEEE Std 802.1X-2010, 11.3, Figure 11-1
struct EapolFrame {
    uint8_t version;
    uint8_t packet_type;
    uint16_t packet_body_length;
    uint8_t packet_body[];
} __PACKED;

}  // namespace wlan
