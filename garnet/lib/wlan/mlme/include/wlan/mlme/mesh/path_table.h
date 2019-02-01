// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/time.h>
#include <wlan/common/macaddr.h>
#include <optional>
#include <unordered_map>

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_PATH_TABLE_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_PATH_TABLE_H_

namespace wlan {

struct MeshPath {
    // Next mesh node in path
    common::MacAddr next_hop;
    // HWMP Sequence Number. Absent if unknown.
    std::optional<uint32_t> hwmp_seqno;
    zx::time expiration_time;
    uint32_t metric;
    unsigned hop_count;
    // The spec also suggests storing a list of "precursors",
    // which doesn't seem necessary so far.
};

struct MeshProxyInfo {
    common::MacAddr mesh_target;
    std::optional<uint32_t> hwmp_seqno;
    zx::time expiration_time;
};

class PathTable {
   public:
    const MeshPath* GetPath(const common::MacAddr& mesh_target) const;
    const MeshPath* AddOrUpdatePath(const common::MacAddr& mesh_target, const MeshPath& path);

    const MeshProxyInfo* GetProxyInfo(const common::MacAddr& target) const;

   private:
    std::unordered_map<uint64_t, std::unique_ptr<MeshPath>> path_by_mesh_target_;
    std::unordered_map<uint64_t, std::unique_ptr<MeshProxyInfo>> proxy_info_by_dest_;
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_PATH_TABLE_H_
