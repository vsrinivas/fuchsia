// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/packet_utils.h>

namespace wlan {

wlan_tx_info_t MakeTxInfo(const FrameControl& fc, uint8_t cbw, uint16_t phy) {
    wlan_tx_info_t txinfo = {
        .tx_flags = 0x0,
        .valid_fields =
            WLAN_TX_INFO_VALID_PHY | WLAN_TX_INFO_VALID_CHAN_WIDTH | WLAN_TX_INFO_VALID_MCS,
        .phy = phy,
        .cbw = cbw,
    };

    // TODO(porce): Implement rate selection.
    switch (fc.type()) {
    // Outgoing data frames.
    case FrameType::kData:
        txinfo.mcs = 0x7;
        break;
        // Outgoing management and control frames.
    default:
        txinfo.mcs = 0x3;  // TODO(NET-645): Choose an optimal MCS
        break;
    }

    if (fc.protected_frame()) { txinfo.tx_flags |= WLAN_TX_INFO_FLAGS_PROTECTED; }

    return txinfo;
}

}  // namespace wlan