// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/mlme/ap/remote_client_interface.h>
#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/macaddr_map.h>

#include <bitmap/raw-bitmap.h>
#include <bitmap/storage.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_ptr.h>
#include <wlan/common/macaddr.h>
#include <zircon/types.h>

#include <list>

namespace wlan {

// Map which tracks clients and assigns AIDs.
class BssClientMap {
   public:
    static constexpr aid_t kMinClientAid = 1;

    BssClientMap() { aid_bitmap_.Reset(kMaxBssClients); }

    bool Has(const common::MacAddr& addr);
    zx_status_t Add(const common::MacAddr& addr, fbl::unique_ptr<RemoteClientInterface> client);
    zx_status_t Remove(const common::MacAddr& addr);
    RemoteClientInterface* GetClient(const common::MacAddr& addr);
    zx_status_t AssignAid(const common::MacAddr& addr, aid_t* out_aid);
    zx_status_t ReleaseAid(const common::MacAddr& addr);
    aid_t GetClientAid(const common::MacAddr& addr);
    void Clear();

   private:
    struct RemoteClient {
        aid_t aid = kUnknownAid;
        fbl::unique_ptr<RemoteClientInterface> handler = nullptr;
    };
    using ClientMap = std::unordered_map<common::MacAddr, RemoteClient, common::MacAddrHasher>;

    bool HasAidAvailable();
    void ClearAid(aid_t aid);

    // Map to lookup clients by their address.
    ClientMap clients_;
    bitmap::RawBitmapGeneric<bitmap::FixedStorage<kMaxBssClients>> aid_bitmap_;
};
}  // namespace wlan
