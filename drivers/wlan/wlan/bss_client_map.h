// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "frame_handler.h"
#include "macaddr_map.h"

#include "garnet/drivers/wlan/common/macaddr.h"

#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>
#include <list>

namespace wlan {

using aid_t = uint16_t;
static constexpr aid_t kUnknownAid = 2009;

enum class RemoteClientState {
    kAuthenticated,
    kAssociated,
    // TODO(hahnr): Add RSNA support.
};

// Map which tracks clients and assigns AIDs.
class BssClientMap {
   public:
    struct RemoteClient {
        aid_t aid;
        RemoteClientState state;
    };
    static constexpr aid_t kMaxClients = 2008;

    BssClientMap() { (void)clients_; }

    bool HasAidAvailable();
    bool Has(const common::MacAddr& addr);
    zx_status_t Add(const common::MacAddr& addr);
    zx_status_t Remove(const common::MacAddr& addr);
    zx_status_t AssignAid(const common::MacAddr& addr, aid_t* out_aid);

   private:
    using ClientList = std::list<RemoteClient>;
    using ClientMap =
        std::unordered_map<common::MacAddr, ClientList::iterator, common::MacAddrHasher>;

    // The next available AID. Automatically updated when clients get an AID assigned or get
    // removed.
    aid_t next_aid_ = 0;
    // Map to lookup clients by their address.
    ClientMap clients_map_;
    // List of clients sorted by their AID.
    ClientList clients_;
};
}  // namespace wlan