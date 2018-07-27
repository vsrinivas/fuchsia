// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/frame_handler.h>

namespace wlan {

// TODO(NET-500): This interface has no abstraction benefit and should be removed entirely.
// A minimum client definition representing a remote client. A client's
// specifics should be opaque to its owner, for example a BSS. This minimalistic
// definition guarantees this constraint.
class RemoteClientInterface : public FrameHandler {
   public:
    virtual ~RemoteClientInterface() = default;

    virtual void HandleTimeout() = 0;
    virtual zx_status_t HandleAnyFrame(fbl::unique_ptr<Packet>) = 0;
};

}  // namespace wlan
