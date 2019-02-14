// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_PARSE_MP_ACTION_H_
#define GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_PARSE_MP_ACTION_H_

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/buffer_reader.h>

namespace wlan {

bool ParseMpOpenAction(BufferReader* r, ::fuchsia::wlan::mlme::MeshPeeringOpenAction* out);

bool ParseMpConfirmAction(BufferReader* r, ::fuchsia::wlan::mlme::MeshPeeringConfirmAction* out);

}  // namespace wlan

#endif  // GARNET_LIB_WLAN_MLME_INCLUDE_WLAN_MLME_MESH_PARSE_MP_ACTION_H_
