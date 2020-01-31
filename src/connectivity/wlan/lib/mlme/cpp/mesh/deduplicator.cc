// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/status.h>

#include <wlan/common/channel.h>
#include <wlan/mlme/beacon.h>
#include <wlan/mlme/device_caps.h>
#include <wlan/mlme/mesh/deduplicator.h>
#include <wlan/mlme/mesh/mesh_mlme.h>
#include <wlan/mlme/mesh/parse_mp_action.h>
#include <wlan/mlme/mesh/write_mp_action.h>
#include <wlan/mlme/service.h>

namespace wlan {

bool DeDuplicator::DeDuplicate(const common::MacAddr& addr, uint32_t seq) {
  auto addr_seq_pair = MacAddrSeqPair(addr, seq);
  if (cache_of_received_packets_.find(addr_seq_pair) != cache_of_received_packets_.end()) {
    return true;
  }

  // Cache the address+seqno
  cache_of_received_packets_.insert(addr_seq_pair);
  queue_of_received_packets_.push(addr_seq_pair);

  // If the queue is growing beyond the max, pop the pair from the front
  // and also remove it from the hash table
  if (queue_of_received_packets_.size() > cache_size_) {
    const MacAddrSeqPair& pair_to_remove = queue_of_received_packets_.front();
    cache_of_received_packets_.erase(pair_to_remove);
    queue_of_received_packets_.pop();
  }
  return false;
}

}  // namespace wlan
