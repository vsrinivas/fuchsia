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
    ~StateMachine() {
        if (state_ != nullptr) { state_->OnExit(); }
    }

    void MoveToState(fbl::unique_ptr<S> state) {
        ZX_DEBUG_ASSERT(state != nullptr);

        if (state_ != nullptr) { state_->OnExit(); }

        if (state != nullptr) {
            state_ = fbl::move(state);
            state_->OnEnter();
        } else {
            errorf("[fsm] state must not be null\n");
        }
    }

    S* state() { return state_.get(); }

   private:
    fbl::unique_ptr<S> state_;
};

}  // namespace fsm
}  // namespace wlan
