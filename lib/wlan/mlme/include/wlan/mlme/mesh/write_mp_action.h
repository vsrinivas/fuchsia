// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/wlan/mlme/cpp/fidl.h>
#include <wlan/common/buffer_writer.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/mac_header_writer.h>

namespace wlan {

void WriteMpOpenActionFrame(BufferWriter* w,
                            const MacHeaderWriter& mac_header_writer,
                            const ::fuchsia::wlan::mlme::MeshPeeringOpenAction& action);

void WriteMpConfirmActionFrame(BufferWriter* w,
                               const MacHeaderWriter& mac_header_writer,
                               const ::fuchsia::wlan::mlme::MeshPeeringConfirmAction& action);

} // namespace wlan
