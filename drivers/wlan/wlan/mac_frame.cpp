// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_frame.h"
#include "packet.h"

#include <ddk/protocol/wlan.h>
#include <fbl/algorithm.h>

namespace wlan {

namespace {

// IEEE Std 802.11-2016, 9.3.3.3
element_id::ElementId kValidBeaconIds[] = {
    element_id::kSsid,
    element_id::kSuppRates,
    element_id::kDsssParamSet,
    element_id::kCfParamSet,
    element_id::kIbssParamSet,
    element_id::kTim,
    element_id::kCountry,
    element_id::kPowerConstraint,
    element_id::kChannelSwitchAnn,
    element_id::kQuiet,
    element_id::kIbssDfs,
    element_id::kTpcReport,
    element_id::kErp,
    element_id::kExtSuppRates,
    element_id::kRsn,
    element_id::kBssLoad,
    element_id::kEdcaParamSet,
    element_id::kQosCapability,
    element_id::kApChannelReport,
    element_id::kBssAvgAccessDelay,
    element_id::kAntenna,
    element_id::kBssAvailAdmissionCapacity,
    element_id::kBssAcAccessDelay,
    element_id::kMeasurementPilotTrans,
    element_id::kMultipleBssid,
    element_id::kRmEnabledCapabilities,
    element_id::kMobilityDomain,
    element_id::kDseRegisteredLocation,
    element_id::kExtChannelSwitchAnn,
    element_id::kSuppOperatingClasses,
    element_id::kHtCapabilities,
    element_id::kHtOperation,
    element_id::k2040BssCoex,
    element_id::kOverlappingBssScanParams,
    element_id::kExtCapabilities,
    element_id::kFmsDescriptor,
    element_id::kQosTrafficCapability,
    element_id::kTimeAdvertisement,
    element_id::kInterworking,
    element_id::kAdvertisementProtocol,
    element_id::kRoamingConsortium,
    element_id::kEmergencyAlertId,
    element_id::kMeshId,
    element_id::kMeshConfiguration,
    element_id::kMeshAwakeWindow,
    element_id::kBeaconTiming,
    element_id::kMccaopAdvertisementOverview,
    element_id::kMccaopAdvertisement,
    element_id::kMeshChannelSwitchParams,
    element_id::kQmfPolicy,
    element_id::kQloadReport,
    element_id::kHccaTxopUpdateCount,
    element_id::kMultiband,
    element_id::kVhtCapabilities,
    element_id::kVhtOperation,
    element_id::kTransmitPowerEnvelope,
    element_id::kChannelSwitchWrapper,
    element_id::kExtBssLoad,
    element_id::kQuietChannel,
    element_id::kOperatingModeNotification,
    element_id::kReducedNeighborReport,
    element_id::kTvhtOperation,
    element_id::kElementWithExtension,  // Estimated Service Parameters
    element_id::kElementWithExtension,  // Future Channel Guidance
    element_id::kVendorSpecific,
};
// IEEE Std 802.11-2016, 9.3.3.10
element_id::ElementId kValidProbeRequestIds[] = {
    element_id::kSsid,
    element_id::kSuppRates,
    element_id::kRequest,
    element_id::kExtSuppRates,
    element_id::kDsssParamSet,
    element_id::kSuppOperatingClasses,
    element_id::kHtCapabilities,
    element_id::k2040BssCoex,
    element_id::kExtCapabilities,
    element_id::kSsidList,
    element_id::kChannelUsage,
    element_id::kInterworking,
    element_id::kMeshId,
    element_id::kMultiband,
    element_id::kDmgCapabilities,
    element_id::kMultipleMacSublayers,
    element_id::kVhtCapabilities,
    element_id::kElementWithExtension,
    element_id::kVendorSpecific,
};

// IEEE Std 802.11-2016, 9.3.3.6
element_id::ElementId kValidAssociationRequestIds[] = {
    element_id::kSsid,
    element_id::kSuppRates,
    element_id::kExtSuppRates,
    element_id::kPowerCapability,
    element_id::kSupportedChannels,
    element_id::kRsn,
    element_id::kQosCapability,
    element_id::kRmEnabledCapabilities,
    element_id::kMobilityDomain,
    element_id::kSuppOperatingClasses,
    element_id::kHtCapabilities,
    element_id::k2040BssCoex,
    element_id::kExtCapabilities,
    element_id::kQosTrafficCapability,
    element_id::kTimBroadcastRequest,
    element_id::kInterworking,
    element_id::kMultiband,
    element_id::kDmgCapabilities,
    element_id::kMultipleMacSublayers,
    element_id::kVhtCapabilities,
    element_id::kOperatingModeNotification,
    element_id::kVendorSpecific,
};

bool ValidateElements(size_t len, element_id::ElementId* ids, size_t ids_len, ElementReader* r) {
    if (!ids || !r) return false;
    size_t idx = 0;
    // Iterate through the elements of the reader, ensuring that each element is in the
    // ids list and that they appear in the proper order.
    // TODO(tkilbourn): handle required vs optional elements
    while (r->is_valid()) {
        const ElementHeader* hdr = r->peek();
        if (hdr == nullptr) { return false; }
        while (idx < ids_len && hdr->id != ids[idx]) {
            idx++;
        }
        if (idx == ids_len) {
            // We reached the end of the valid ids without finding this one, so it's an invalid id.
            return false;
        }
        r->skip(*hdr);
    }
    // Ensure we've read all the data from the reader.
    return r->offset() == len;
}
}  // namespace

bool Beacon::Validate(size_t len) {
    ElementReader reader(elements, len);
    constexpr size_t kValidIdSize = fbl::count_of(kValidProbeRequestIds);
    return ValidateElements(len, kValidBeaconIds, kValidIdSize, &reader);
}

bool ProbeRequest::Validate(size_t len) {
    ElementReader reader(elements, len);
    constexpr size_t kValidIdSize = fbl::count_of(kValidProbeRequestIds);
    return ValidateElements(len, kValidProbeRequestIds, kValidIdSize, &reader);
}

bool AssociationRequest::Validate(size_t len) {
    ElementReader reader(elements, len);
    constexpr size_t kValidIdSize = fbl::count_of(kValidAssociationRequestIds);
    return ValidateElements(len, kValidAssociationRequestIds, kValidIdSize, &reader);
}

// TODO(porce): Consider zx_status_t return type
template <typename Body>
MgmtFrame<Body> BuildMgmtFrame(fbl::unique_ptr<Packet>* packet, size_t body_payload_len,
                               bool has_ht_ctrl) {
    size_t hdr_len = sizeof(MgmtFrameHeader) + (has_ht_ctrl ? kHtCtrlLen : 0);
    size_t body_len = sizeof(Body) + body_payload_len;
    size_t frame_len = hdr_len + body_len;

    *packet = Packet::CreateWlanPacket(frame_len);
    if (*packet == nullptr) { return MgmtFrame<Body>(nullptr, nullptr, 0); }

    // Zero out the packet buffer by default for the management frame.
    (*packet)->clear();

    auto hdr = (*packet)->mut_field<MgmtFrameHeader>(0);
    hdr->fc.set_subtype(Body::Subtype());
    if (has_ht_ctrl) { hdr->fc.set_htc_order(1); }

    auto body = (*packet)->mut_field<Body>(hdr->len());
    return MgmtFrame<Body>(hdr, body, body_len);
}

#define DECLARE_BUILD_MGMTFRAME(bodytype)                                        \
    template MgmtFrame<bodytype> BuildMgmtFrame(fbl::unique_ptr<Packet>* packet, \
                                                size_t body_payload_len, bool has_ht_ctrl)

DECLARE_BUILD_MGMTFRAME(ProbeRequest);

zx_status_t FillTxInfo(fbl::unique_ptr<Packet>* packet, const MgmtFrameHeader& hdr) {
    // TODO(porce): Evolve the API to use FrameHeader
    // and support all types of frames.
    ZX_DEBUG_ASSERT(packet != nullptr && *packet);
    wlan_tx_info_t txinfo = {
        // Outgoing management frame
        .tx_flags = 0x0,
        .valid_fields = WLAN_TX_INFO_VALID_PHY | WLAN_TX_INFO_VALID_CHAN_WIDTH,
        .phy = WLAN_PHY_OFDM,  // Always
        .cbw = CBW20,          // Use CBW20 always
    };

    // TODO(porce): Imeplement rate selection.
    switch (hdr.fc.subtype()) {
    default:
        txinfo.data_rate = 12;  // 6 Mbps, one of the basic rates.
        txinfo.mcs = 0x1;       // TODO(porce): Merge data_rate into MCS.
        break;
    }

    (*packet)->CopyCtrlFrom(txinfo);
    return ZX_OK;
}

}  // namespace wlan
