// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>

namespace wlan {
namespace fsm {

class StateInterface {
   public:
    virtual void OnEnter() {}
    virtual void OnExit() {}
};

template <typename S> class StateMachine {
   public:
    void MoveToState(fbl::unique_ptr<S> state) {
        state_->OnExit();
        state_ = fbl::move(state);
        state_->OnEnter();
    }

    S* state() { return state_.get(); }

   private:
    fbl::unique_ptr<S> state_;
};

}  // namespace fsm
}  // namespace wlan
