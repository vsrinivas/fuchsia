// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MESH_HWMP_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MESH_HWMP_H_

#include <wlan/common/buffer_reader.h>
#include <wlan/mlme/mac_header_writer.h>
#include <wlan/mlme/mesh/path_table.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/rate_limiter.h>
#include <wlan/mlme/timer_manager.h>

namespace wlan {

struct HwmpState {
  struct TimedEvent {
    common::MacAddr addr;
  };

  struct TargetState {
    TimeoutId next_attempt;
    size_t attempts_left;
  };

  uint32_t our_hwmp_seqno = 0;
  uint32_t next_path_discovery_id = 0;
  TimerManager<TimedEvent> timer_mgr;
  std::unordered_map<uint64_t, TargetState> state_by_target;
  RateLimiter perr_rate_limiter;

  explicit HwmpState(fbl::unique_ptr<Timer> timer);
};

PacketQueue HandleHwmpAction(Span<const uint8_t> elements,
                             const common::MacAddr& action_transmitter_addr,
                             const common::MacAddr& self_addr,
                             uint32_t last_hop_metric,
                             const MacHeaderWriter& mac_header_writer,
                             HwmpState* state, PathTable* path_table);

zx_status_t InitiatePathDiscovery(const common::MacAddr& target_addr,
                                  const common::MacAddr& self_addr,
                                  const MacHeaderWriter& mac_header_writer,
                                  HwmpState* state, const PathTable& path_table,
                                  PacketQueue* packets_to_tx);

zx_status_t HandleHwmpTimeout(const common::MacAddr& self_addr,
                              const MacHeaderWriter& mac_header_writer,
                              HwmpState* state, const PathTable& path_table,
                              PacketQueue* packets_to_tx);

PacketQueue OnMissingForwardingPath(const common::MacAddr& peer_to_notify,
                                    const common::MacAddr& missing_destination,
                                    const MacHeaderWriter& mac_header_writer,
                                    HwmpState* state);

// Visible for testing
bool HwmpSeqnoLessThan(uint32_t a, uint32_t b);

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MESH_HWMP_H_
