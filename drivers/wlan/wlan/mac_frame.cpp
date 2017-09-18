// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_frame.h"

#include <fbl/algorithm.h>

namespace wlan {

namespace {
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

}  // namespace wlan
