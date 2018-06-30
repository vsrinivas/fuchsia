// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>
#include <wlan/mlme/frame_handler.h>
#include <wlan/mlme/packet.h>
#include <zircon/types.h>

namespace wlan {

class BaseMlmeMsg;

zx_status_t DispatchFramePacket(fbl::unique_ptr<Packet> packet, FrameHandler* target);

}  // namespace wlan
