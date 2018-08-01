// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mcs.h>

#include <math.h>

namespace wlan {

SupportedMcsSet IntersectMcs(const SupportedMcsSet& lhs, const SupportedMcsSet& rhs) {
    // Find an intersection.
    // Perform bitwise-AND bitmasksi fields, which represent MCS
    // Take minimum of numeric values

    auto result = SupportedMcsSet{};
    result.rx_mcs_head.set_bitmask(lhs.rx_mcs_head.bitmask() & rhs.rx_mcs_head.bitmask());
    result.rx_mcs_tail.set_bitmask(lhs.rx_mcs_tail.bitmask() & rhs.rx_mcs_tail.bitmask());
    result.rx_mcs_tail.set_highest_rate(
        std::min(lhs.rx_mcs_tail.highest_rate(), rhs.rx_mcs_tail.highest_rate()));
    result.tx_mcs.set_set_defined(lhs.tx_mcs.set_defined() & rhs.tx_mcs.set_defined());
    result.tx_mcs.set_rx_diff(lhs.tx_mcs.rx_diff() & rhs.tx_mcs.rx_diff());
    result.tx_mcs.set_max_ss(std::min(lhs.tx_mcs.max_ss(), rhs.tx_mcs.max_ss()));
    result.tx_mcs.set_ueqm(lhs.tx_mcs.ueqm() & rhs.tx_mcs.ueqm());

    return result;
}

}  // namespace wlan
