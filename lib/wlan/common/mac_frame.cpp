// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/mac_frame.h>

#include <fbl/algorithm.h>
#include <wlan/protocol/mac.h>

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
// IEEE Std 802.11-2016, 9.3.3.10
element_id::ElementId kValidProbeResponseIds[] = {
    element_id::kSsid,
    element_id::kSuppRates,
    element_id::kDsssParamSet,
    element_id::kCfParamSet,
    element_id::kIbssParamSet,
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
    element_id::kMeasurementPilotTrans,
    element_id::kMultipleBssid,
    element_id::kRmEnabledCapabilities,
    element_id::kApChannelReport,
    element_id::kBssAvgAccessDelay,
    element_id::kAntenna,
    element_id::kBssAvailAdmissionCapacity,
    element_id::kBssAcAccessDelay,
    element_id::kMobilityDomain,
    element_id::kDseRegisteredLocation,
    element_id::kExtChannelSwitchAnn,
    element_id::kSuppOperatingClasses,
    element_id::kHtCapabilities,
    element_id::kHtOperation,
    element_id::k2040BssCoex,
    element_id::kOverlappingBssScanParams,
    element_id::kExtCapabilities,
    element_id::kQosTrafficCapability,
    element_id::kChannelUsage,
    element_id::kTimeAdvertisement,
    element_id::kTimeZone,
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
    element_id::kMultiband,
    element_id::kDmgCapabilities,
    element_id::kDmgOperation,
    element_id::kMultipleMacSublayers,
    element_id::kAntennaSectorIdPattern,
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
    element_id::kRelayCapabilities,
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

// IEEE Std 802.11-2016, 9.3.3.7
element_id::ElementId kValidAssociationResponseIds[] = {
    element_id::kSuppRates,
    element_id::kExtSuppRates,
    element_id::kEdcaParamSet,
    element_id::kRcpi,
    element_id::kRsni,
    element_id::kRmEnabledCapabilities,
    element_id::kMobilityDomain,
    element_id::kFastBssTransition,
    element_id::kDseRegisteredLocation,
    element_id::kTimeoutInterval,
    element_id::kHtCapabilities,
    element_id::kHtOperation,
    element_id::k2040BssCoex,
    element_id::kOverlappingBssScanParams,
    element_id::kExtCapabilities,
    element_id::kBssMaxIdlePeriod,
    element_id::kTimBroadcastResponse,
    element_id::kQosMap,
    element_id::kQmfPolicy,
    element_id::kMultiband,
    element_id::kDmgCapabilities,
    element_id::kDmgOperation,
    element_id::kMultipleMacSublayers,
    element_id::kNeighborReport,
    element_id::kVhtCapabilities,
    element_id::kVhtOperation,
    element_id::kOperatingModeNotification,
    element_id::kMultipleMacSublayers,
    element_id::kVhtCapabilities,
    element_id::kOperatingModeNotification,
    element_id::kElementWithExtension,  // Future Channel Guidance
    element_id::kVendorSpecific,
};

bool ValidateElements(size_t len, element_id::ElementId* ids, size_t ids_len, ElementReader* r) {
    if (!ids || !r) return false;
    size_t idx = 0;
    // Iterate through the elements of the reader, ensuring that each element is in the ids list and
    // that they appear in the proper order.
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
    constexpr size_t kValidIdSize = fbl::count_of(kValidBeaconIds);
    return ValidateElements(len, kValidBeaconIds, kValidIdSize, &reader);
}

bool ProbeRequest::Validate(size_t len) {
    ElementReader reader(elements, len);
    constexpr size_t kValidIdSize = fbl::count_of(kValidProbeRequestIds);
    return ValidateElements(len, kValidProbeRequestIds, kValidIdSize, &reader);
}

bool ProbeResponse::Validate(size_t len) {
    ElementReader reader(elements, len);
    constexpr size_t kValidIdSize = fbl::count_of(kValidProbeResponseIds);
    return ValidateElements(len, kValidProbeResponseIds, kValidIdSize, &reader);
}

bool AssociationRequest::Validate(size_t len) {
    ElementReader reader(elements, len);
    constexpr size_t kValidIdSize = fbl::count_of(kValidAssociationRequestIds);
    return ValidateElements(len, kValidAssociationRequestIds, kValidIdSize, &reader);
}

bool AssociationResponse::Validate(size_t len) {
    ElementReader reader(elements, len);
    constexpr size_t kValidIdSize = fbl::count_of(kValidAssociationResponseIds);
    return ValidateElements(len, kValidAssociationResponseIds, kValidIdSize, &reader);
}

CapabilityInfo IntersectCapInfo(const CapabilityInfo& lhs, const CapabilityInfo& rhs) {
    auto cap_info = CapabilityInfo{};

    cap_info.set_ess(lhs.ess() & rhs.ess());
    cap_info.set_ibss(lhs.ibss() & rhs.ibss());
    cap_info.set_cf_pollable(lhs.cf_pollable() & rhs.cf_pollable());
    cap_info.set_cf_poll_req(lhs.cf_poll_req() & rhs.cf_poll_req());
    cap_info.set_privacy(lhs.privacy() & rhs.privacy());
    cap_info.set_short_preamble(lhs.short_preamble() & rhs.short_preamble());
    cap_info.set_spectrum_mgmt(lhs.spectrum_mgmt() & rhs.spectrum_mgmt());
    cap_info.set_qos(lhs.qos() & rhs.qos());
    // TODO(NET-1267): Revisit short slot time when necessary.
    // IEEE 802.11-2016 11.1.3.2 and 9.4.1.4
    cap_info.set_short_slot_time(lhs.short_slot_time() & rhs.short_slot_time());
    cap_info.set_apsd(lhs.apsd() & rhs.apsd());
    cap_info.set_radio_msmt(lhs.radio_msmt() & rhs.radio_msmt());
    cap_info.set_delayed_block_ack(lhs.delayed_block_ack() & rhs.delayed_block_ack());
    cap_info.set_immediate_block_ack(lhs.immediate_block_ack() & rhs.immediate_block_ack());

    return cap_info;
}

}  // namespace wlan
