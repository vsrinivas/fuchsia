// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <wlan/mlme/remote_client.h>

#include <wlan/mlme/mac_frame.h>
#include <wlan/mlme/packet.h>

namespace wlan {

#define LOG_STATE_TRANSITION(addr, from, to) \
    debugbss("[client] [%s] %s -> %s\n", addr.ToString().c_str(), from, to);

// BaseState implementation.

template <typename S, typename... Args> void BaseState::MoveToState(Args&&... args) {
    static_assert(fbl::is_base_of<BaseState, S>::value, "State class must implement BaseState");
    client_->MoveToState(fbl::make_unique<S>(client_, std::forward<Args>(args)...));
}

// DeauthenticatedState implementation.

DeauthenticatedState::DeauthenticatedState(RemoteClient* client) : BaseState(client) {
}

zx_status_t DeauthenticatedState::HandleAuthentication(
    const ImmutableMgmtFrame<Authentication>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    debugbss("[client] [%s] received Authentication request...\n",
             client_->addr().ToString().c_str());

    auto auth_alg = frame.body->auth_algorithm_number;
    if (auth_alg != AuthAlgorithm::kOpenSystem) {
        errorf("[client] [%s] received auth attempt with unsupported algorithm: %u\n",
               client_->addr().ToString().c_str(), auth_alg);
        return client_->SendAuthentication(status_code::kUnsupportedAuthAlgorithm);
    }

    auto auth_txn_seq_no = frame.body->auth_txn_seq_number;
    if (auth_txn_seq_no != 1) {
        errorf("[client] [%s] received auth attempt with invalid tx seq no: %u\n",
               client_->addr().ToString().c_str(), auth_txn_seq_no);
        return client_->SendAuthentication(status_code::kRefused);
    }

    auto status = client_->SendAuthentication(status_code::kSuccess);
    if (status == ZX_OK) {
        MoveToState<AuthenticatedState>();
        LOG_STATE_TRANSITION(client_->addr(), "Deauthenticated", "Authenticated");
    }
    return status;
}

// AuthenticatedState implementation.

AuthenticatedState::AuthenticatedState(RemoteClient* client) : BaseState(client) {
}

void AuthenticatedState::OnEnter() {
    // Start timeout and wait for Association requests.
    auth_timeout_ = client_->CreateTimerDeadline(kAuthenticationTimeoutTu);
    client_->StartTimer(auth_timeout_);
}

void AuthenticatedState::OnExit() {
    client_->CancelTimer();
    auth_timeout_ = zx::time();
}

void AuthenticatedState::HandleTimeout() {
    if (client_->IsDeadlineExceeded(auth_timeout_)) {
        MoveToState<DeauthenticatedState>();
        LOG_STATE_TRANSITION(client_->addr(), "Authenticated", "Deauthenticated");
    }
}

zx_status_t AuthenticatedState::HandleAuthentication(
    const ImmutableMgmtFrame<Authentication>& frame, const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Authentication request while being authenticated\n",
             client_->addr().ToString().c_str());
    // Client is already authenticated but seems to not have received the previous Authentication
    // response which was sent. Hence, let the client know its authentication was successful.
    // TODO(hahnr): We should process the authentication frame again?
    return client_->SendAuthentication(status_code::kSuccess);
}

zx_status_t AuthenticatedState::HandleDeauthentication(const ImmutableMgmtFrame<Deauthentication>& frame,
                                                    const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Deauthentication request: %u\n",
             client_->addr().ToString().c_str(), frame.body->reason_code);
    MoveToState<DeauthenticatedState>();
    LOG_STATE_TRANSITION(client_->addr(), "Authenticated", "Deauthenticated");
    return ZX_OK;
}

zx_status_t AuthenticatedState::HandleAssociationRequest(
    const ImmutableMgmtFrame<AssociationRequest>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request\n", client_->addr().ToString().c_str());

    // Received request which we've been waiting for. Timer can get canceled.
    client_->CancelTimer();
    auth_timeout_ = zx::time();

    aid_t aid;
    auto status = client_->bss()->AssignAid(client_->addr(), &aid);
    if (status == ZX_ERR_NO_RESOURCES) {
        debugbss("[client] [%s] no more AIDs available \n", client_->addr().ToString().c_str());
        client_->SendAssociationResponse(0, status_code::kDeniedNoMoreStas);
        // TODO(hahnr): Unclear whether we should deauth the client or not. Check existing AP
        // implementations for their behavior. For now, let the client stay authenticated.
        return ZX_OK;
    } else if (status != ZX_OK) {
        errorf("[client] [%s] couldn't assign AID to client: %d\n",
               client_->addr().ToString().c_str(), status);
        return ZX_OK;
    }

    // TODO(hahnr): Send MLME-Authenticate.indication and wait for response.
    // For now simply send association response.
    client_->SendAssociationResponse(aid, status_code::kSuccess);
    MoveToState<AssociatedState>(aid);
    LOG_STATE_TRANSITION(client_->addr(), "Authenticated", "Associated");
    return status;
}

// AssociatedState implementation.

AssociatedState::AssociatedState(RemoteClient* client, uint16_t aid)
    : BaseState(client), aid_(aid) {
}

zx_status_t AssociatedState::HandleAssociationRequest(
    const ImmutableMgmtFrame<AssociationRequest>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    debugbss("[client] [%s] received Assocation Request while being associated\n",
             client_->addr().ToString().c_str());

    // Even though the client is already associated, Association requests should still be answered.
    // This can happen when the client for some reasons did not receive the previous
    // AssociationResponse the BSS sent and keeps sending Association requests.
    return client_->SendAssociationResponse(aid_, status_code::kSuccess);
}

void AssociatedState::OnEnter() {
    debugbss("[client] [%s] acquired AID: %u\n", client_->addr().ToString().c_str(), aid_);

    inactive_timeout_ = client_->CreateTimerDeadline(kInactivityTimeoutTu);
    client_->StartTimer(inactive_timeout_);
    debugbss("[client] [%s] started inactivity timer\n", client_->addr().ToString().c_str());
}

zx_status_t AssociatedState::HandleDeauthentication(const ImmutableMgmtFrame<Deauthentication>& frame,
                                                    const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Deauthentication request: %u\n",
             client_->addr().ToString().c_str(), frame.body->reason_code);
    MoveToState<DeauthenticatedState>();
    LOG_STATE_TRANSITION(client_->addr(), "Associated", "Deauthenticated");
    return ZX_OK;
}

zx_status_t AssociatedState::HandleDisassociation(const ImmutableMgmtFrame<Disassociation>& frame,
                                                  const wlan_rx_info_t& rxinfo) {
    debugbss("[client] [%s] received Disassociation request: %u\n",
             client_->addr().ToString().c_str(), frame.body->reason_code);
    MoveToState<AuthenticatedState>();
    LOG_STATE_TRANSITION(client_->addr(), "Associated", "Authenticated");
    return ZX_OK;
}

void AssociatedState::OnExit() {
    client_->CancelTimer();
    inactive_timeout_ = zx::time();

    // Ensure the client's AID is released when association is broken.
    client_->bss()->ReleaseAid(client_->addr());
    debugbss("[client] [%s] released AID: %u\n", client_->addr().ToString().c_str(), aid_);
}

zx_status_t AssociatedState::HandleDataFrame(const DataFrameHeader& hdr) {
    active_ = true;
    return ZX_OK;
}

zx_status_t AssociatedState::HandleDataFrame(const ImmutableDataFrame<LlcHeader>& frame,
                                     const wlan_rx_info_t& rxinfo) {
    // Filter unsupported Data frame types.
    switch (frame.hdr->fc.subtype()) {
        case DataSubtype::kDataSubtype:
            // Fall-through
        case DataSubtype::kQosdata:  // For data frames within BlockAck session.
            break;
        default:
            warnf("unsupported data subtype %02x\n", frame.hdr->fc.subtype());
            return ZX_OK;
    }

    if (frame.hdr->fc.to_ds() == 0 || frame.hdr->fc.from_ds() == 1) {
        warnf("received unsupported data frame from %s with to_ds/from_ds combination: %u/%u\n",
              frame.hdr->addr2.ToString().c_str(), frame.hdr->fc.to_ds(), frame.hdr->fc.from_ds());
        return ZX_OK;
    }

    auto hdr = frame.hdr;
    auto llc = frame.body;

    // Forward EAPOL frames to SME.
    if (be16toh(llc->protocol_id) == kEapolProtocolId) {
        // TODO(NET-462): Forward EAPOL frames to Wlanstack.
        return ZX_OK;
    }

    // TODO(NET-463): Disallow data frames if RSNA is required but not established.
    // TODO(NET-445): Check FC's power saving bit.

    const size_t eth_len = frame.body_len + sizeof(EthernetII);
    auto buffer = GetBuffer(eth_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto eth_packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), eth_len));
    // no need to clear the packet since every byte is overwritten
    eth_packet->set_peer(Packet::Peer::kEthernet);

    auto eth = eth_packet->mut_field<EthernetII>(0);
    eth->dest = hdr->addr3;
    eth->src = hdr->addr2;
    eth->ether_type = llc->protocol_id;
    std::memcpy(eth->payload, llc->payload, frame.body_len - sizeof(LlcHeader));

    auto status = client_->SendEthernet(std::move(eth_packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send ethernet data: %d\n", client_->addr().ToString().c_str(), status);
    }

    return status;
}

zx_status_t AssociatedState::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    active_ = true;
    return ZX_OK;
}

void AssociatedState::HandleTimeout() {
    if (!client_->IsDeadlineExceeded(inactive_timeout_)) { return; }

    if (active_) {
        active_ = false;

        // Client was active, restart timer.
        debugbss("[client] [%s] client is active; reset inactive timer\n",
                 client_->addr().ToString().c_str());
        inactive_timeout_ = client_->CreateTimerDeadline(kInactivityTimeoutTu);
        client_->StartTimer(inactive_timeout_);
    } else {
        active_ = false;

        // The client timed-out, send Deauthentication. Ignore result, always leave associated
        // state.
        client_->SendDeauthentication(reason_code::ReasonCode::kReasonInactivity);
        debugbss("[client] [%s] client inactive for %lu seconds; deauthenticating client\n",
                 client_->addr().ToString().c_str(), kInactivityTimeoutTu / 1000);
        MoveToState<DeauthenticatedState>();
        LOG_STATE_TRANSITION(client_->addr(), "Associated", "Deauthenticated");
    }
}

// RemoteClient implementation.

RemoteClient::RemoteClient(DeviceInterface* device, fbl::unique_ptr<Timer> timer, BssInterface* bss,
                           RemoteClient::Listener* listener, const common::MacAddr& addr)
    : listener_(listener), device_(device), bss_(bss), addr_(addr), timer_(std::move(timer)) {
    ZX_DEBUG_ASSERT(device != nullptr);
    ZX_DEBUG_ASSERT(timer != nullptr);
    ZX_DEBUG_ASSERT(bss != nullptr);
    debugbss("[client] [%s] spawned\n", addr_.ToString().c_str());

    MoveToState(fbl::make_unique<DeauthenticatedState>(this));
    LOG_STATE_TRANSITION(addr_, "(init)", "Deauthenticated");
}

RemoteClient::~RemoteClient() {
    debugbss("[client] [%s] destroyed\n", addr_.ToString().c_str());
}

void RemoteClient::MoveToState(fbl::unique_ptr<BaseState> to) {
    auto from_id = state() == nullptr ? RemoteClient::StateId::kUninitialized : state()->id();
    fsm::StateMachine<BaseState>::MoveToState(fbl::move(to));
    if (listener_ != nullptr) { listener_->HandleClientStateChange(addr_, from_id, to->id()); }
}

void RemoteClient::HandleTimeout() {
    state()->HandleTimeout();
}

zx_status_t RemoteClient::HandleDataFrame(const DataFrameHeader& hdr) {
    ZX_DEBUG_ASSERT(hdr.addr2 == addr_);
    if (hdr.addr2 != addr_) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(state());
    return ZX_OK;
}

zx_status_t RemoteClient::HandleMgmtFrame(const MgmtFrameHeader& hdr) {
    ZX_DEBUG_ASSERT(hdr.addr2 == addr_);
    if (hdr.addr2 != addr_) { return ZX_ERR_STOP; }

    ForwardCurrentFrameTo(state());
    return ZX_OK;
}

zx_status_t RemoteClient::StartTimer(zx::time deadline) {
    CancelTimer();
    return timer_->SetTimer(deadline);
}

zx_status_t RemoteClient::CancelTimer() {
    return timer_->CancelTimer();
}

zx::time RemoteClient::CreateTimerDeadline(zx_duration_t tus) {
    return timer_->Now() + WLAN_TU(tus);
}

bool RemoteClient::IsDeadlineExceeded(zx::time deadline) {
    return deadline > zx::time() && timer_->Now() >= deadline;
}

zx_status_t RemoteClient::SendAuthentication(status_code::StatusCode result) {
    debugfn();
    debugbss("[client] [%s] sending Authentication response\n", addr_.ToString().c_str());

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Authentication>(&packet);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    FillTxInfo(&packet, *frame.hdr);

    auto hdr = frame.hdr;
    hdr->fc.set_from_ds(1);
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(device_->GetState()->next_seq());

    auto auth = frame.body;
    auth->status_code = result;
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    // TODO(hahnr): Evolve this to support other authentication algorithms and track seq number.
    auth->auth_txn_seq_number = 2;

    auto status = device_->SendWlan(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendAssociationResponse(aid_t aid, status_code::StatusCode result) {
    debugfn();
    debugbss("[client] [%s] sending Association Response\n", addr_.ToString().c_str());

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<AssociationResponse>(&packet);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    hdr->fc.set_from_ds(1);
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(device_->GetState()->next_seq());

    auto assoc = frame.body;
    assoc->status_code = result;
    assoc->aid = aid;
    assoc->cap.set_ess(1);
    assoc->cap.set_short_preamble(1);

    auto status = device_->SendWlan(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send auth response packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendDeauthentication(reason_code::ReasonCode reason_code) {
    debugfn();
    debugbss("[client] [%s] sending Disassociation\n", addr_.ToString().c_str());

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Deauthentication>(&packet);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto hdr = frame.hdr;
    hdr->fc.set_from_ds(1);
    hdr->addr1 = addr_;
    hdr->addr2 = bss_->bssid();
    hdr->addr3 = bss_->bssid();
    hdr->sc.set_seq(device_->GetState()->next_seq());

    auto deauth = frame.body;
    deauth->reason_code = reason_code;

    auto status = device_->SendWlan(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[client] [%s] could not send disassocation packet: %d\n", addr_.ToString().c_str(),
               status);
    }
    return status;
}

zx_status_t RemoteClient::SendEthernet(fbl::unique_ptr<Packet> packet) {
    return device_->SendEthernet(fbl::move(packet));
}

#undef LOG_STATE_TRANSITION

}  // namespace wlan
