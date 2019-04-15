// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MESH_DEDUPLICATOR_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MESH_DEDUPLICATOR_H_

#include <wlan/common/buffer_reader.h>
#include <wlan/common/parse_mac_header.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/mac_header_writer.h>
#include <wlan/mlme/mesh/hwmp.h>
#include <wlan/mlme/mesh/path_table.h>
#include <wlan/mlme/mlme.h>

#include <queue>
#include <unordered_set>

namespace wlan {

class DeDuplicator {
 public:
  explicit DeDuplicator(size_t cache_size) { cache_size_ = cache_size; }

  // Function to check if this packet has been seen before.
  bool DeDuplicate(const common::MacAddr& addr, uint32_t seq);

 private:
  typedef std::pair<common::MacAddr, uint32_t> MacAddrSeqPair;
  struct addr_seq_hash {
    // This hash function combines a 6-byte mac address and 4-byte
    // sequence number into an 8-byte hash by putting mac address
    // in the top 6 bytes and the lower 2 bytes of sequence number
    // in the last 2 bytes.
    inline std::size_t operator()(const MacAddrSeqPair& pkt) {
      return pkt.first.ToU64() + pkt.second;
    }
  };

  // The queue is used to limit the size of the cache
  std::queue<MacAddrSeqPair> queue_of_received_packets_;
  size_t cache_size_;

  // The unordered_set is used to cache the received packets for duplicate
  // detection
  std::unordered_set<MacAddrSeqPair, addr_seq_hash> cache_of_received_packets_;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_MESH_DEDUPLICATOR_H_
