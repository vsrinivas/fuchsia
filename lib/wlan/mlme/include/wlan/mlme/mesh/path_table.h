// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/macaddr.h>
#include <optional>

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_PATH_TABLE_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_PATH_TABLE_H_

namespace wlan {

struct MeshPath {
    // Next mesh node in path
    common::MacAddr next_hop;
    // Last mesh node in path
    common::MacAddr mesh_target;
};

class PathTable {
   public:
    std::optional<MeshPath> GetPath(const common::MacAddr& target);
};

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_PATH_TABLE_H_
