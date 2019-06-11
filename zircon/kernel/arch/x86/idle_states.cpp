// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <assert.h>

#include <arch/x86/feature.h>
#include <arch/x86/idle_states.h>

const x86_idle_states_t* x86_get_idle_states() {
    return &x86_get_microarch_config()->idle_states;
}

int x86_num_idle_states(const x86_idle_states_t* states) {
    int num;
    for (num = 0; num < X86_MAX_CSTATES; ++num) {
        if (x86_is_base_idle_state(states->states + num)) {
            return num + 1;
        }
    }
    // The config must include and end with X86_BASE_CSTATE.
    return -1;
}

X86IdleStates::X86IdleStates(const x86_idle_states_t* states) {
    num_states_ = x86_num_idle_states(states);
    ASSERT_MSG(num_states_ > 0,
               "Invalid C-state configuration: Expected at least C1 to be defined.");
    for (int i = 0; i < num_states_; ++i) {
        states_[i] = X86IdleState(states->states + i);
    }
}

X86IdleState* X86IdleStates::PickIdleState() {
    // Return the shallowest state (C1).
    // TODO(jfsulliv): Implement state selection.
    return states_ + (num_states_ - 1);
}
