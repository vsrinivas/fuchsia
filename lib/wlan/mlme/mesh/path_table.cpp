// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/mesh/path_table.h>

namespace wlan {

std::optional<MeshPath> PathTable::GetPath(const common::MacAddr& target) {
    // TODO(gbonik): implement an actual path table & HWMP
    return {{ target, target }};
}

} // namespace wlan
