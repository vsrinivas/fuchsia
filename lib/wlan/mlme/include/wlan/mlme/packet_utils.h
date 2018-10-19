// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_PACKET_UTILS_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_PACKET_UTILS_H_

#include <wlan/common/mac_frame.h>
#include <wlan/protocol/info.h>
#include <wlan/protocol/mac.h>

namespace wlan {

wlan_tx_info_t MakeTxInfo(const FrameControl& fc, uint8_t cbw, uint16_t phy, uint32_t flags = 0);

}  // namespace wlan
#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_PACKET_UTILS_H_
