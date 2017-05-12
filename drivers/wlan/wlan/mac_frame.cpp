// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mac_frame.h"

#include <mxtl/algorithm.h>

namespace wlan {

namespace {
// IEEE Std 802.11-2016, 9.3.3.10
ElementId kValidProbeRequestIds[] = {
    kSsid,
    kSuppRates,
    kRequest,
    kExtSuppRates,
    kDsssParamSet,
    kSuppOperatingClasses,
    kHtCapabilities,
    k2040BssCoex,
    kExtCapabilities,
    kSsidList,
    kChannelUsage,
    kInternetworking,
    kMeshId,
    kMultiband,
    kDmgCapabilities,
    kMultipleMacSublayers,
    kVhtCapabilities,
    kElementWithExtension,
    kVendorSpecific,
};
}  // namespace

bool ProbeRequest::Validate(size_t len) {
    ElementReader reader(elements, len);
    size_t idx = 0;
    constexpr size_t kEndIndex = mxtl::count_of(kValidProbeRequestIds);
    // Iterate through the elements of the ProbeRequest, ensuring that each element is in the
    // valid_ids_ list and that they appear in the proper order.
    // TODO(tkilbourn): handle required vs optional elements
    while (reader.is_valid()) {
        const ElementHeader* hdr = reader.peek();
        if (hdr == nullptr) {
            return false;
        }
        while (idx < kEndIndex && hdr->id != kValidProbeRequestIds[idx]) {
            idx++;
        }
        if (idx == kEndIndex) {
            // We reached the end of the valid ids without finding this one, so it's an invalid id.
            return false;
        }
        reader.skip(*hdr);
    }
    // Ensure we've read all the data from the ProbeRequest.
    return reader.offset() == len;
}

}  // namespace wlan
