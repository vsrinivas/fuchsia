// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mesh/path_table.h>

namespace wlan {

const MeshPath* PathTable::GetPath(const common::MacAddr& mesh_target) const {
  auto it = path_by_mesh_target_.find(mesh_target.ToU64());
  if (it == path_by_mesh_target_.end()) {
    return nullptr;
  }
  return it->second.get();
}

void PathTable::RemovePath(const common::MacAddr& mesh_target) {
  path_by_mesh_target_.erase(mesh_target.ToU64());
}

const MeshPath* PathTable::AddOrUpdatePath(const common::MacAddr& mesh_target,
                                           const MeshPath& path) {
  auto key = mesh_target.ToU64();
  auto it = path_by_mesh_target_.find(key);
  if (it != path_by_mesh_target_.end()) {
    *it->second = path;
    return it->second.get();
  }
  auto p = path_by_mesh_target_.insert({key, std::make_unique<MeshPath>(path)});
  return p.first->second.get();
}

const MeshProxyInfo* PathTable::GetProxyInfo(
    const common::MacAddr& target) const {
  auto it = proxy_info_by_dest_.find(target.ToU64());
  if (it == proxy_info_by_dest_.end()) {
    return nullptr;
  }
  return it->second.get();
}

}  // namespace wlan
