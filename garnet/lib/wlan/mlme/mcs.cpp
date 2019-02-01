// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mcs.h>

#include <math.h>

namespace wlan {
namespace wlan_mlme = ::fuchsia::wlan::mlme;

SupportedMcsSet IntersectMcs(const SupportedMcsSet& lhs, const wlan_mlme::SupportedMcsSet& fidl) {
    return IntersectMcs(lhs, SupportedMcsSetFromFidl(fidl));
}

SupportedMcsSet IntersectMcs(const wlan_mlme::SupportedMcsSet& fidl, const SupportedMcsSet& lhs) {
    return IntersectMcs(lhs, SupportedMcsSetFromFidl(fidl));
}

SupportedMcsSet SupportedMcsSetFromFidl(const wlan_mlme::SupportedMcsSet& fidl) {
    SupportedMcsSet mcs_set;

    mcs_set.rx_mcs_head.set_bitmask(fidl.rx_mcs_set);
    // mcs_set.rx_mcs_tail.set_bitmask() unimplemented (no info in fidl)
    mcs_set.rx_mcs_tail.set_highest_rate(fidl.rx_highest_rate);
    mcs_set.tx_mcs.set_set_defined(fidl.tx_mcs_set_defined ? 1 : 0);
    mcs_set.tx_mcs.set_rx_diff(fidl.tx_rx_diff ? 1 : 0);

    // Beware: the differece in the meaning of max_ss, depending on the data structure.
    ZX_DEBUG_ASSERT(fidl.tx_max_ss > 0);
    mcs_set.tx_mcs.set_max_ss_human(fidl.tx_max_ss);
    mcs_set.tx_mcs.set_ueqm(fidl.tx_ueqm ? 1 : 0);

    return mcs_set;
}

}  // namespace wlan
