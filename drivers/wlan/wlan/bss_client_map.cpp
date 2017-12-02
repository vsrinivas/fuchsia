// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bss_client_map.h"

namespace wlan {

bool BssClientMap::HasAidAvailable() {
    return next_aid_ < kMaxClients;
}

bool BssClientMap::Has(const common::MacAddr& addr) {
    return clients_map_.find(addr) != clients_map_.end();
}

zx_status_t BssClientMap::Add(const common::MacAddr& addr) {
    if (Has(addr)) { return ZX_ERR_ALREADY_EXISTS; }

    // Insert client in list with an unkown AID.
    auto client = RemoteClient{
        .aid = kUnknownAid, .state = RemoteClientState::kAuthenticated,
    };
    clients_.push_back(std::move(client));
    clients_map_.emplace(addr, std::prev(clients_.end()));

    return ZX_OK;
}

zx_status_t BssClientMap::Remove(const common::MacAddr& addr) {
    ZX_DEBUG_ASSERT(Has(addr));

    if (!Has(addr)) { return ZX_ERR_NOT_FOUND; }

    // Remove client from map and list.
    auto map_pos = clients_map_.find(addr);
    auto list_pos = map_pos->second;
    auto client = *list_pos;
    clients_.erase(list_pos);
    clients_map_.erase(map_pos);

    // Update next_aid_.
    if (client.aid < next_aid_) { next_aid_ = client.aid; }

    return ZX_OK;
}

zx_status_t BssClientMap::AssignAid(const common::MacAddr& addr, aid_t* out_aid) {
    ZX_DEBUG_ASSERT(Has(addr));

    *out_aid = kUnknownAid;
    if (!Has(addr)) {
        return ZX_ERR_NOT_FOUND;
    }

    // Update the client's state and its AID.
    auto map_pos = clients_map_.find(addr);
    auto list_pos = map_pos->second;
    auto client = *list_pos;
    client.state = RemoteClientState::kAssociated;
    // Do not assign a new AID to the client if the client has already one assigned.
    if (client.aid != kUnknownAid) {
        *out_aid = client.aid;
        return ZX_OK;
    }
    client.aid = next_aid_;
    *out_aid = client.aid;

    // Move client to correct in order position in list and update map.
    auto insert_pos = std::lower_bound(clients_.begin(), clients_.end(), client.aid,
                                       [](RemoteClient& c, aid_t aid) { return c.aid < aid; });
    if (insert_pos != list_pos) {
        clients_.splice(insert_pos, clients_, list_pos, std::next(list_pos));
        map_pos->second = insert_pos;
    }
    auto iter = map_pos->second;

    // Update next_aid_.
    do {
        next_aid_++;
        iter++;
    } while (next_aid_ < kMaxClients && iter != clients_.end() && iter->aid == next_aid_);
    return ZX_OK;
}

}  // namespace wlan