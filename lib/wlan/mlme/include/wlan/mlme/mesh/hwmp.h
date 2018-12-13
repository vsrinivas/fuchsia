// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_HWMP_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_HWMP_H_

#include <wlan/common/buffer_reader.h>
#include <wlan/mlme/mac_header_writer.h>
#include <wlan/mlme/mesh/path_table.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/timer_manager.h>

namespace wlan {

struct HwmpState {
    uint32_t our_hwmp_seqno;
    TimerManager timer_mgr;

    explicit HwmpState(fbl::unique_ptr<Timer> timer)
        : our_hwmp_seqno(0), timer_mgr(std::move(timer)) {}
};

PacketQueue HandleHwmpAction(Span<const uint8_t> elements,
                             const common::MacAddr& action_transmitter_addr,
                             const common::MacAddr& self_addr, uint32_t last_hop_metric,
                             const MacHeaderWriter& mac_header_writer, HwmpState* state,
                             PathTable* path_table);

// Visible for testing
bool HwmpSeqnoLessThan(uint32_t a, uint32_t b);

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_HWMP_H_
