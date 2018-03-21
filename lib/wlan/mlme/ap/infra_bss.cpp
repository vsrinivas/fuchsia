// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/ap/infra_bss.h>

#include <wlan/mlme/mlme.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/service.h>

#include <zircon/syscalls.h>

namespace wlan {

InfraBss::InfraBss(DeviceInterface* device, fbl::unique_ptr<BeaconSender> bcn_sender,
                   const common::MacAddr& bssid)
    : bssid_(bssid), device_(device), bcn_sender_(fbl::move(bcn_sender)) {
    ZX_DEBUG_ASSERT(bcn_sender_ != nullptr);
}

InfraBss::~InfraBss() {
    // The BSS should always be explicitly stopped.
    // Throw in debug builds, stop in release ones.
    ZX_DEBUG_ASSERT(!IsStarted());

    // Ensure BSS is stopped correctly.
    Stop();
}

void InfraBss::Start(const StartRequest& req) {
    if (IsStarted()) { return; }

    // Move to requested channel.
    auto chan = wlan_channel_t{
        .primary = req.channel,
        .cbw = CBW20,
    };
    auto status = device_->SetChannel(chan);
    if (status != ZX_OK) {
        errorf("[infra-bss] [%s] requested start on channel %u failed: %d\n",
               bssid_.ToString().c_str(), req.channel, status);
    }

    debugbss("[infra-bss] [%s] starting BSS\n", bssid_.ToString().c_str());
    debugbss("    SSID: %s\n", req.ssid->data());
    debugbss("    Beacon Period: %u\n", req.beacon_period);
    debugbss("    DTIM Period: %u\n", req.dtim_period);
    debugbss("    Channel: %u\n", req.channel);

    // Start sending Beacon frames.
    started_at_ = zx_clock_get(ZX_CLOCK_MONOTONIC);
    bcn_sender_->Start(this, req);
}

void InfraBss::Stop() {
    if (!IsStarted()) { return; }

    debugbss("[infra-bss] [%s] stopping BSS\n", bssid_.ToString().c_str());

    bcn_sender_->Stop();
    started_at_ = 0;
    clients_.Clear();
}

bool InfraBss::IsStarted() {
    return started_at_ != 0;
}

zx_status_t InfraBss::HandleTimeout(const common::MacAddr& client_addr) {
    ZX_DEBUG_ASSERT(clients_.Has(client_addr));
    if (clients_.Has(client_addr)) { clients_.GetClient(client_addr)->HandleTimeout(); }
    return ZX_OK;
}

zx_status_t InfraBss::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    // Drop management frames which are not targeted towards this BSS.
    // TODO(hahnr): Need to support wildcard BSSID for ProbeRequests.
    if (bssid_ != hdr.addr1 || bssid_ != hdr.addr3) { return ZX_ERR_STOP; }

    // Let the correct RemoteClient instance process the received frame.
    auto& client_addr = hdr.addr2;
    if (clients_.Has(client_addr)) { ForwardCurrentFrameTo(clients_.GetClient(client_addr)); }
    return ZX_OK;
}

zx_status_t InfraBss::HandleDataFrame(const DataFrameHeader& hdr) {
    if (bssid_ != hdr.addr1) { return ZX_ERR_STOP; }

    // Let the correct RemoteClient instance process the received frame.
    auto& client_addr = hdr.addr2;
    if (clients_.Has(client_addr)) { ForwardCurrentFrameTo(clients_.GetClient(client_addr)); }
    return ZX_OK;
}

zx_status_t InfraBss::HandleAuthentication(const ImmutableMgmtFrame<Authentication>& frame,
                                           const wlan_rx_info_t& rxinfo) {
    // If the client is already known, there is no work to be done here.
    auto& client_addr = frame.hdr->addr2;
    if (clients_.Has(client_addr)) { return ZX_OK; }

    // Else, create a new remote client instance.
    fbl::unique_ptr<Timer> timer = nullptr;
    auto status = CreateClientTimer(client_addr, &timer);
    if (status != ZX_OK) { return status; }

    auto client = fbl::make_unique<RemoteClient>(device_, fbl::move(timer),
                                                 this,  // bss
                                                 this,  // client listener
                                                 client_addr);
    clients_.Add(client_addr, fbl::move(client));

    // Note: usually, HandleMgmtFrame(...) will forward incoming frames to the corresponding
    // clients. However, Authentication frames will add new clients and hence, this frame must be
    // forwarded manually to the newly added client.
    ForwardCurrentFrameTo(clients_.GetClient(client_addr));
    return ZX_OK;
}

zx_status_t InfraBss::HandlePsPollFrame(const ImmutableCtrlFrame<PsPollFrame>& frame,
                                        const wlan_rx_info_t& rxinfo) {
    auto& client_addr = frame.hdr->ta;
    if (frame.hdr->bssid != bssid_) { return ZX_ERR_STOP; }
    if (clients_.GetClientAid(client_addr) != frame.hdr->aid) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(clients_.GetClient(client_addr));
    return ZX_OK;
}

void InfraBss::HandleClientStateChange(const common::MacAddr& client, RemoteClient::StateId from,
                                       RemoteClient::StateId to) {
    debugfn();
    // Ignore when transitioning from `uninitialized` state.
    if (from == RemoteClient::StateId::kUninitialized) {
        return;
    }

    ZX_DEBUG_ASSERT(clients_.Has(client));
    if (!clients_.Has(client)) {
        errorf("state change (%hhu, %hhu) reported for unknown client: %s\n", from, to,
               client.ToString().c_str());
        return;
    }

    // If client enters deauthenticated state after being authenticated, remove client.
    if (to == RemoteClient::StateId::kDeauthenticated) {
        auto status = clients_.Remove(client);
        if (status != ZX_OK) {
            errorf("[infra-bss] couldn't remove client %s: %d\n", client.ToString().c_str(),
                   status);
        }
    }
}

void InfraBss::HandleClientBuChange(const common::MacAddr& client, size_t bu_count) {
    debugfn();
    auto aid = clients_.GetClientAid(client);
    ZX_DEBUG_ASSERT(aid != kUnknownAid);
    if (aid == kUnknownAid) {
        errorf("[infra-bss] received traffic indication from client with unknown AID: %s\n",
               client.ToString().c_str());
        return;
    }

    tim_.SetTrafficIndication(aid, bu_count > 0);
    bcn_sender_->UpdateBeacon(tim_);
}

zx_status_t InfraBss::AssignAid(const common::MacAddr& client, aid_t* out_aid) {
    debugfn();
    auto status = clients_.AssignAid(client, out_aid);
    if (status != ZX_OK) {
        errorf("[infra-bss] couldn't assign AID to client %s: %d\n", client.ToString().c_str(),
               status);
        return status;
    }
    return ZX_OK;
}

zx_status_t InfraBss::ReleaseAid(const common::MacAddr& client) {
    debugfn();
    auto aid = clients_.GetClientAid(client);
    ZX_DEBUG_ASSERT(aid != kUnknownAid);
    if (aid == kUnknownAid) {
        errorf("[infra-bss] tried releasing AID for unknown client: %s\n",
               client.ToString().c_str());
        return ZX_ERR_NOT_FOUND;
    }

    tim_.SetTrafficIndication(aid, false);
    bcn_sender_->UpdateBeacon(tim_);
    return clients_.ReleaseAid(client);
}

fbl::unique_ptr<Buffer> InfraBss::GetPowerSavingBuffer(size_t len) {
    return GetBuffer(len);
}

zx_status_t InfraBss::CreateClientTimer(const common::MacAddr& client_addr,
                                        fbl::unique_ptr<Timer>* out_timer) {
    ObjectId timer_id;
    timer_id.set_subtype(to_enum_type(ObjectSubtype::kTimer));
    timer_id.set_target(to_enum_type(ObjectTarget::kBss));
    timer_id.set_mac(client_addr.ToU64());
    zx_status_t status =
        device_->GetTimer(ToPortKey(PortKeyType::kMlme, timer_id.val()), out_timer);
    if (status != ZX_OK) {
        errorf("could not create bss timer: %d\n", status);
        return status;
    }
    return ZX_OK;
}

const common::MacAddr& InfraBss::bssid() const {
    return bssid_;
}

uint64_t InfraBss::timestamp() {
    zx_time_t now = zx_clock_get(ZX_CLOCK_MONOTONIC);
    zx_duration_t uptime_ns = now - started_at_;
    return uptime_ns / 1000; // as microseconds
}

seq_t InfraBss::NextSeq(const MgmtFrameHeader& hdr) {
    return NextSeqNo(hdr, &seq_);
}

seq_t InfraBss::NextSeq(const MgmtFrameHeader& hdr, uint8_t aci) {
    return NextSeqNo(hdr, aci, &seq_);
}

seq_t InfraBss::NextSeq(const DataFrameHeader& hdr) {
    return NextSeqNo(hdr, &seq_);
}

}  // namespace wlan
