// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/bss_client_map.h>

namespace wlan {

bool BssClientMap::Has(const common::MacAddr& addr) {
    return clients_.find(addr) != clients_.end();
}

zx_status_t BssClientMap::Add(const common::MacAddr& addr,
                              fbl::unique_ptr<RemoteClientInterface> handler) {
    if (Has(addr)) { return ZX_ERR_ALREADY_EXISTS; }
    clients_.emplace(addr, RemoteClient{.handler = std::move(handler)});
    return ZX_OK;
}

zx_status_t BssClientMap::Remove(const common::MacAddr& addr) {
    ZX_DEBUG_ASSERT(Has(addr));
    if (!Has(addr)) { return ZX_ERR_NOT_FOUND; }

    // Remove client and release AID if the client had one assigned.
    auto iter = clients_.find(addr);
    auto aid = iter->second.aid;
    if (aid != kUnknownAid) { ClearAid(aid); }
    clients_.erase(iter);

    return ZX_OK;
}

RemoteClientInterface* BssClientMap::GetClient(const common::MacAddr& addr) {
    if (!Has(addr)) { return nullptr; }

    auto iter = clients_.find(addr);
    auto& client = iter->second;
    return client.handler.get();
}

zx_status_t BssClientMap::AssignAid(const common::MacAddr& addr, aid_t* out_aid) {
    ZX_DEBUG_ASSERT(Has(addr));
    *out_aid = kUnknownAid;
    if (!HasAidAvailable()) { return ZX_ERR_NO_RESOURCES; }
    if (!Has(addr)) { return ZX_ERR_NOT_FOUND; }

    // Update the client's state and its AID.
    auto iter = clients_.find(addr);
    auto& client = iter->second;
    // Do not assign a new AID to the client if the client has already one
    // assigned.
    if (client.aid != kUnknownAid) {
        *out_aid = client.aid;
        return ZX_OK;
    }

    // Retrieve next available AID. Return if all AIDs are already taken.
    aid_t available_aid;
    auto status = aid_bitmap_.Get(kMinClientAid, kMaxBssClients, &available_aid);
    if (status != ZX_OK) { return status; }

    status = aid_bitmap_.SetOne(available_aid);
    if (status != ZX_OK) { return status; }
    client.aid = available_aid;
    *out_aid = client.aid;
    return ZX_OK;
}

zx_status_t BssClientMap::ReleaseAid(const common::MacAddr& addr) {
    ZX_DEBUG_ASSERT(Has(addr));
    if (!Has(addr)) { return ZX_ERR_NOT_FOUND; }

    auto iter = clients_.find(addr);
    auto& client = iter->second;
    ClearAid(client.aid);
    client.aid = kUnknownAid;

    return ZX_OK;
}

aid_t BssClientMap::GetClientAid(const common::MacAddr& addr) {
    ZX_DEBUG_ASSERT(Has(addr));
    if (!Has(addr)) { return kUnknownAid; }

    auto iter = clients_.find(addr);
    return iter->second.aid;
}

bool BssClientMap::HasAidAvailable() {
    return !aid_bitmap_.Get(kMinClientAid, kMaxBssClients);
}

void BssClientMap::ClearAid(aid_t aid) {
    ZX_DEBUG_ASSERT(aid < kMaxBssClients);
    if (aid != kUnknownAid) { aid_bitmap_.ClearOne(aid); }
}

void BssClientMap::Clear() {
    aid_bitmap_.Reset(kMaxBssClients);
    clients_.clear();
}

}  // namespace wlan