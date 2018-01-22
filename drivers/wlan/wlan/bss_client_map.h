// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "frame_handler.h"
#include "macaddr_map.h"

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
#include <zircon/types.h>

#include <list>

namespace wlan {

using aid_t = size_t;
static constexpr aid_t kUnknownAid = 2009;

// TODO(hahnr): Remove and replace with RemoteClient state machine.
enum class RemoteClientState {
    kAuthenticated,
    kAssociated,
};

// Map which tracks clients and assigns AIDs.
class BssClientMap {
   public:
    struct RemoteClient {
        aid_t aid = kUnknownAid;
        RemoteClientState state = RemoteClientState::kAuthenticated;
    };
    static constexpr aid_t kMaxClients = 2008;

    BssClientMap() { (void)clients_; }

    bool HasAidAvailable();
    bool Has(const common::MacAddr& addr);
    zx_status_t Add(const common::MacAddr& addr);
    zx_status_t Remove(const common::MacAddr& addr);
    zx_status_t AssignAid(const common::MacAddr& addr, aid_t* out_aid);

   private:
    using ClientMap = std::unordered_map<common::MacAddr, RemoteClient, common::MacAddrHasher>;

    // Map to lookup clients by their address.
    ClientMap clients_;
    bitmap::RawBitmapGeneric<bitmap::FixedStorage<kMaxClients>> aid_bitmap_;
};
}  // namespace wlan
