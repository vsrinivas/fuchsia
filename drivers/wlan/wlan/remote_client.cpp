// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remote_client.h"

namespace wlan {
namespace remote_client {

InitState::InitState(fsm::StateMachine<BaseState>* client) : BaseState(client) {}

AuthenticatedState::AuthenticatedState(fsm::StateMachine<BaseState>* client) : BaseState(client) {}

zx_status_t AuthenticatedState::HandleAssociationResponse(
    const MgmtFrame<AssociationResponse>& frame, const wlan_rx_info_t& rxinfo) {
    uint16_t aid;

    // ...

    auto assoc_state = fbl::make_unique<AssociatedState>(client_, aid);
    client_->MoveToState(fbl::move(assoc_state));

    return ZX_OK;
}

AssociatedState::AssociatedState(fsm::StateMachine<BaseState>* client, uint16_t aid)
    : BaseState(client), aid_(aid) {
    (void)aid_;
}

RemoteClient::RemoteClient() {
    fbl::unique_ptr<BaseState> init_state = fbl::make_unique<InitState>(this);
    MoveToState(fbl::move(init_state));
}

}  // namespace remote_client
}  // namespace wlan
