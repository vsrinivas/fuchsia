// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/common/macaddr.h>
#include <optional>

#pragma once

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

} // namespace wlan
