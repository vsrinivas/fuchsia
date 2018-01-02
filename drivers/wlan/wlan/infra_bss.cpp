// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dispatcher.h"
#include "infra_bss.h"
#include "packet.h"
#include "serialize.h"

namespace wlan {

zx_status_t InfraBss::HandleTimeout(const common::MacAddr& client_addr) {
    if (clients_.Has(client_addr)) {
        // TODO(hahnr): Notify remote client about timeout.
    }
    return ZX_OK;
}

zx_status_t InfraBss::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    // Drop management frames which are not targeted towards this BSS.
    if (bssid_ != hdr.addr1 || bssid_ != hdr.addr3) {
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

zx_status_t InfraBss::HandleAuthentication(const MgmtFrame<Authentication>& frame,
                                           const wlan_rx_info_t& rxinfo) {
    debugfn();

    auto& client_addr = frame.hdr->addr2;
    auto auth_alg = frame.body->auth_algorithm_number;
    if (auth_alg != AuthAlgorithm::kOpenSystem) {
        errorf("[infra-bss] received auth attempt with unsupported algorithm: %u\n", auth_alg);
        SendAuthentication(client_addr, status_code::kUnsupportedAuthAlgorithm);
        return ZX_ERR_STOP;
    }

    auto auth_txn_seq_no = frame.body->auth_txn_seq_number;
    if (auth_txn_seq_no != 1) {
        errorf("[infra-bss] received auth attempt with invalid tx seq no: %u\n", auth_txn_seq_no);
        SendAuthentication(client_addr, status_code::kRefused);
        return ZX_ERR_STOP;
    }

    // Authentication request are always responded to, no matter if the client is already known or
    // not.
    if (!clients_.Has(client_addr)) { clients_.Add(client_addr); }

    SendAuthentication(client_addr, status_code::kSuccess);
    return ZX_ERR_STOP;
}

zx_status_t InfraBss::SendAuthentication(const common::MacAddr& dst,
                                         status_code::StatusCode result) {
    debugfn();

    size_t body_len = sizeof(Authentication);
    fbl::unique_ptr<Packet> packet;
    auto auth = CreateMgmtFrame<Authentication>(dst, kAuthentication, body_len, &packet);
    if (auth == nullptr) { return ZX_ERR_NO_RESOURCES; }
    auth->status_code = result;
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    // TODO(hahnr): Evolve this to support other authentication algorithms and track seq number.
    auth->auth_txn_seq_number = 2;

    auto status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("[infra-bss] could not send auth response packet: %d\n", status);
        return status;
    }
    return ZX_OK;
}

zx_status_t InfraBss::HandleAssociationRequest(const MgmtFrame<AssociationRequest>& frame,
                                                 const wlan_rx_info_t& rxinfo) {
    debugfn();

    auto& client_addr = frame.hdr->addr2;
    if (!clients_.Has(client_addr)) {
        errorf("[infra-bss] received assoc req from unknown client: %s\n", MACSTR(client_addr));
        return ZX_ERR_STOP;
    }

    if (!clients_.HasAidAvailable()) {
        errorf("[infra-bss] received assoc req but reached max allowed clients: %s\n",
               MACSTR(client_addr));
        SendAssociationResponse(client_addr, status_code::kDeniedNoMoreStas);
        return ZX_ERR_STOP;
    }

    // TODO(hahnr): Verify capabilities, ssid, rates, rsn, etc.
    // For now simply send association response.
    SendAssociationResponse(client_addr, status_code::kSuccess);
    // TODO(hahnr): Create RemoteClient and pass timer created via CreateClientTimer(client_addr).
    return ZX_ERR_STOP;
}

zx_status_t InfraBss::SendAssociationResponse(const common::MacAddr& dst,
                                              status_code::StatusCode result) {
    debugfn();
    ZX_DEBUG_ASSERT(clients_.Has(dst));

    aid_t aid = kUnknownAid;
    auto status = clients_.AssignAid(dst, &aid);
    if (status != ZX_OK) {
        errorf("[infra-bss] couldn't assign AID to client: %d, %s\n", status, MACSTR(dst));
        return ZX_OK;
    }

    // Note: The response is also sent for already associated clients. In this case the client's
    // already assigned AID is reused.
    size_t body_len = sizeof(AssociationResponse);
    fbl::unique_ptr<Packet> packet;
    auto assoc = CreateMgmtFrame<AssociationResponse>(dst, kAssociationResponse, body_len, &packet);
    if (assoc == nullptr) { return ZX_ERR_NO_RESOURCES; }
    assoc->status_code = result;
    assoc->aid = aid;
    assoc->cap.set_ess(1);
    assoc->cap.set_short_preamble(1);

    status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("[infra-bss] could not send auth response packet: %d\n", status);
        return status;
    }
    return ZX_OK;
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
        errorf("could not create scan timer: %d\n", status);
        return status;
    }
    return ZX_OK;
}

template <typename Body>
Body* InfraBss::CreateMgmtFrame(const common::MacAddr& dst, ManagementSubtype subtype,
                                  size_t body_len, fbl::unique_ptr<Packet>* out_packet) {
    size_t len = sizeof(MgmtFrameHeader) + body_len;
    fbl::unique_ptr<Buffer> buffer = GetBuffer(len);
    if (buffer == nullptr) { return nullptr; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);

    // Write header.
    auto hdr = packet->mut_field<MgmtFrameHeader>(0);
    hdr->fc.set_type(kManagement);
    hdr->fc.set_subtype(subtype);
    hdr->addr1 = dst;
    hdr->addr2 = bssid_;
    hdr->addr3 = bssid_;
    hdr->sc.set_seq(next_seq_no());

    *out_packet = fbl::move(packet);
    return (*out_packet)->mut_field<Body>(hdr->len());
}

const common::MacAddr& InfraBss::bssid() {
    return bssid_;
}

uint16_t InfraBss::next_seq_no() {
    return last_seq_no_++ & kMaxSequenceNumber;
}

uint64_t InfraBss::timestamp() {
    bss::timestamp_t now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(now - started_at_).count();
}

}  // namespace wlan