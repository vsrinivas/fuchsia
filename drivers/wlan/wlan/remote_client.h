// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "frame_handler.h"
#include "fsm.h"

#include <zircon/types.h>

namespace wlan {
namespace remote_client {

class BaseState : public fsm::StateInterface, public FrameHandler {
   public:
    BaseState(fsm::StateMachine<BaseState>* client) : client_(client) {}
    virtual ~BaseState() = default;

    virtual void HandleTimeout() {}

   protected:
    fsm::StateMachine<BaseState>* client_;
};

class InitState : public BaseState {
   public:
    InitState(fsm::StateMachine<BaseState>* client);

    // ...
};

class AuthenticatedState : public BaseState {
   public:
    AuthenticatedState(fsm::StateMachine<BaseState>* client);

    zx_status_t HandleAssociationResponse(const MgmtFrame<AssociationResponse>& frame,
                                          const wlan_rx_info_t& rxinfo) override;

    // ...
};

class AssociatedState : public BaseState {
   public:
    AssociatedState(fsm::StateMachine<BaseState>* client, uint16_t aid);

    // ...

   private:
    const uint16_t aid_;
};

class RemoteClient : public fsm::StateMachine<BaseState> {
   public:
    RemoteClient();

    void HandleTimeout() { state()->HandleTimeout(); }

    // ...
};

}  // namespace remote_client
}  // namespace wlan
