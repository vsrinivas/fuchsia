// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/buffer_reader.h>

namespace wlan {

bool ParseMpOpenAction(BufferReader* r, ::fuchsia::wlan::mlme::MeshPeeringOpenAction* out);

} // namespace wlan
