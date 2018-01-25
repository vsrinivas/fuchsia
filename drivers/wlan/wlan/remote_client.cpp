// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_client.h"
#include "packet.h"

namespace wlan {

// BaseState implementation.

template <typename S, typename... Args> void BaseState::MoveToState(Args&&... args) {
    client_->MoveToState(fbl::make_unique<S>(client_, std::forward<Args>(args)...));
}

// DeauthenticatedState implementation.

DeauthenticatedState::DeauthenticatedState(RemoteClient* client) : BaseState(client) {}

zx_status_t DeauthenticatedState::HandleAuthentication(
    const ImmutableMgmtFrame<Authentication>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());

    auto auth_alg = frame.body->auth_algorithm_number;
    if (auth_alg != AuthAlgorithm::kOpenSystem) {
        errorf("[idle-state] received auth attempt with unsupported algorithm: %u\n", auth_alg);
        return client_->SendAuthentication(status_code::kUnsupportedAuthAlgorithm);
    }

    auto auth_txn_seq_no = frame.body->auth_txn_seq_number;
    if (auth_txn_seq_no != 1) {
        errorf("[idle-state] received auth attempt with invalid tx seq no: %u\n", auth_txn_seq_no);
        return client_->SendAuthentication(status_code::kRefused);
    }

    auto status = client_->SendAuthentication(status_code::kSuccess);
    if (status == ZX_OK) { MoveToState<AuthenticatedState>(); }
    return status;
}

// AuthenticatedState implementation.

AuthenticatedState::AuthenticatedState(RemoteClient* client) : BaseState(client) {}

void AuthenticatedState::OnEnter() {
    // Start timeout and wait for Association requests.
    auth_timeout_ = client_->StartTimer(kAuthenticationTimeoutTu);
}

void AuthenticatedState::OnExit() {
    client_->CancelTimer();
    auth_timeout_ = zx::time();
}

void AuthenticatedState::HandleTimeout() {
    if (client_->HasTimerTriggered(auth_timeout_)) {
        MoveToState<DeauthenticatedState>();
    }
}

zx_status_t AuthenticatedState::HandleAssociationRequest(
    const ImmutableMgmtFrame<AssociationRequest>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());

    // Received request which we've been waiting for. Timer can get canceled.
    client_->CancelTimer();
    auth_timeout_ = zx::time();

    aid_t aid;
    auto status = client_->bss()->AssignAid(client_->addr(), &aid);
    if (status == ZX_ERR_NO_RESOURCES) {
        client_->SendAssociationResponse(0, status_code::kDeniedNoMoreStas);
        // TODO(hahnr): Unclear whether we should deauth the client or not. Check existing AP
        // implementations for their behavior. For now, let the client stay authenticated.
        return ZX_OK;
    } else if (status != ZX_OK) {
        errorf("[authed-state] couldn't assign AID to client %s: %d\n", MACSTR(client_->addr()),
               status);
        return ZX_OK;
    }

    // TODO(hahnr): Send MLME-Authenticate.indication and wait for response.
    // For now simply send association response.
    client_->SendAssociationResponse(aid, status_code::kSuccess);
    MoveToState<AssociatedState>(aid);
    return status;
}

// AssociatedState implementation.

AssociatedState::AssociatedState(RemoteClient* client, uint16_t aid)
    : BaseState(client), aid_(aid) {
    // TODO(hahnr): Track inactivity.
}

zx_status_t AssociatedState::HandleAssociationRequest(
    const ImmutableMgmtFrame<AssociationRequest>& frame, const wlan_rx_info_t& rxinfo) {
    debugfn();
    ZX_DEBUG_ASSERT(frame.hdr->addr2 == client_->addr());
    // Even though the client is already associated, Association requests should still be answered.
    // This can happen when the client for some reasons did not receive the previous
    // AssociationResponse the BSS sent and keeps sending Association requests.
    return client_->SendAssociationResponse(aid_, status_code::kSuccess);
}

void AssociatedState::OnExit() {
    // Ensure the client's AID is released when association is broken.
    client_->bss()->ReleaseAid(client_->addr());
}

// RemoteClient implementation.

RemoteClient::RemoteClient(DeviceInterface* device, fbl::unique_ptr<Timer> timer, BssInterface* bss,
                           const common::MacAddr& addr)
    : device_(device), bss_(bss), addr_(addr), timer_(std::move(timer)) {
    MoveToState(fbl::make_unique<DeauthenticatedState>(this));
}

void RemoteClient::HandleTimeout() {
    state()->HandleTimeout();
}

// TODO(hahnr): HandleAnyFrame should be aware of the frame header.
zx_status_t RemoteClient::HandleAnyFrame() {
    ForwardCurrentFrameTo(state());
    return ZX_OK;
}

zx::time RemoteClient::StartTimer(zx_duration_t tus) {
    CancelTimer();
    zx::time deadline = timer_->Now() + WLAN_TU(tus);
    timer_->SetTimer(deadline);
    return deadline;
}

bool RemoteClient::HasTimerTriggered(zx::time deadline) {
    return deadline > zx::time() && timer_->Now() >= deadline;
}

void RemoteClient::CancelTimer() {
    timer_->CancelTimer();
}

zx_status_t RemoteClient::SendAuthentication(status_code::StatusCode result) {
    debugfn();

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<Authentication>(&packet);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    FillTxInfo(&packet, *frame.hdr);

    auto auth = frame.body;
    auth->status_code = result;
    auth->auth_algorithm_number = AuthAlgorithm::kOpenSystem;
    // TODO(hahnr): Evolve this to support other authentication algorithms and track seq number.
    auth->auth_txn_seq_number = 2;

    auto status = device_->SendWlan(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[remote-client] could not send auth response packet: %d\n", status);
    }
    return status;
}

zx_status_t RemoteClient::SendAssociationResponse(aid_t aid, status_code::StatusCode result) {
    debugfn();

    fbl::unique_ptr<Packet> packet = nullptr;
    auto frame = BuildMgmtFrame<AssociationResponse>(&packet);
    if (packet == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto assoc = frame.body;
    assoc->status_code = result;
    assoc->aid = aid;
    assoc->cap.set_ess(1);
    assoc->cap.set_short_preamble(1);

    auto status = device_->SendWlan(fbl::move(packet));
    if (status != ZX_OK) {
        errorf("[remote-client] could not send auth response packet: %d\n", status);
    }
    return status;
}

}  // namespace wlan
