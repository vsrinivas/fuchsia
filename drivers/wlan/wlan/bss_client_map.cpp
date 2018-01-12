// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bss_client_map.h"

namespace wlan {

bool BssClientMap::HasAidAvailable() {
    return !aid_bitmap_.Get(0, kMaxClients - 1);
}

bool BssClientMap::Has(const common::MacAddr& addr) {
    return clients_.find(addr) != clients_.end();
}

zx_status_t BssClientMap::Add(const common::MacAddr& addr) {
    if (Has(addr)) { return ZX_ERR_ALREADY_EXISTS; }
    clients_.emplace(addr, RemoteClient{});
    return ZX_OK;
}

zx_status_t BssClientMap::Remove(const common::MacAddr& addr) {
    ZX_DEBUG_ASSERT(Has(addr));
    if (!Has(addr)) { return ZX_ERR_NOT_FOUND; }

    // Remove client and release AID.
    auto iter = clients_.find(addr);
    auto aid = iter->second.aid;
    aid_bitmap_.ClearOne(aid);
    clients_.erase(iter);

    return ZX_OK;
}

zx_status_t BssClientMap::AssignAid(const common::MacAddr& addr, aid_t* out_aid) {
    ZX_DEBUG_ASSERT(Has(addr));
    *out_aid = kUnknownAid;
    if (!Has(addr)) {
        return ZX_ERR_NOT_FOUND;
    }

    // Update the client's state and its AID.
    auto iter = clients_.find(addr);
    auto& client = iter->second;
    client.state = RemoteClientState::kAssociated;
    // Do not assign a new AID to the client if the client has already one assigned.
    if (client.aid != kUnknownAid) {
        *out_aid = client.aid;
        return ZX_OK;
    }

    // Retrieve next available AID. Return if all AIDs are already taken.
    aid_t available_aid;
    if (aid_bitmap_.Get(0, kMaxClients - 1, &available_aid)) { return ZX_ERR_NO_RESOURCES; }

    auto status = aid_bitmap_.SetOne(available_aid);
    if (status != ZX_OK) { return status; }
    client.aid = available_aid;
    *out_aid = client.aid;
    return ZX_OK;
}

}  // namespace wlan