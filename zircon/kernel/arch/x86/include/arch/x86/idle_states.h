// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <zircon/time.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define X86_MAX_CSTATES 8

typedef struct {
    // A human-readable name for the state.
    const char* name;
    // The value to set the mwait hint register to in order to enter the state.
    uint32_t mwait_hint;
    // The expected latency (in us) of exiting the C-state.
    uint32_t exit_latency;
    // Whether entering the state can result in a TLB flush.
    bool flushes_tlb;
} x86_idle_state_t;

typedef struct {
    // A list of states supported by the system, sorted by descending
    // latency to exit the state.
    // The list must be non-empty and end with X86_BASE_CSTATE; all states after
    // this entry must be ignored.
    x86_idle_state_t states[X86_MAX_CSTATES];
} x86_idle_states_t;

// Every processor must support at least C1.
#define X86_BASE_CSTATE_NAME "C1"
#define X86_BASE_CSTATE_MWAIT_HINT 0x00

#define X86_BASE_CSTATE(exit_latency_us)                                                           \
    {                                                                                              \
        .name = X86_BASE_CSTATE_NAME, .mwait_hint = X86_BASE_CSTATE_MWAIT_HINT,                    \
        .exit_latency = (exit_latency_us), .flushes_tlb = false,                                   \
    }

static inline bool x86_is_base_idle_state(const x86_idle_state_t* state) {
    return state->mwait_hint == X86_BASE_CSTATE_MWAIT_HINT;
}

// Returns a read-only pointer to the list of idle states supported by the
// system.
const x86_idle_states_t* x86_get_idle_states(void);

// Returns the number of states in |states|.
// If |states| is invalid (i.e. does not contain X86_BASE_CSTATE), returns -1.
int x86_num_idle_states(const x86_idle_states_t* states);

#ifndef __cplusplus
typedef struct X86IdleStates X86IdleStates;
#endif // __cplusplus

__END_CDECLS

#ifdef __cplusplus

class X86IdleState {
public:
    X86IdleState()
        : state_(nullptr), times_entered_(0L), total_duration_(0L) {}
    explicit X86IdleState(const x86_idle_state_t* state)
        : state_(state), times_entered_(0L), total_duration_(0L) {}

    const char* Name() const { return state_->name; }

    // Returns the hint to provide to MWAIT to enter this state.
    uint32_t MwaitHint() const { return state_->mwait_hint; }

    // Returns the expected latency (in us) of exiting the C-state.
    uint32_t ExitLatency() const { return state_->exit_latency; }

    bool IsBaseState() const { return x86_is_base_idle_state(state_); }

    bool FlushesTlb() const { return state_->flushes_tlb; }

    // Returns the number of times the system entered this state since the last
    // call to ResetCounters().
    uint64_t TimesEntered() const { return times_entered_; }

    // Returns total time the system has spent in this state since the last
    // call to ResetCounters()
    zx_duration_t CumulativeDuration() const { return total_duration_; }

    // Resets the entry and duration counters.
    void ResetCounters() {
        times_entered_ = 0;
        total_duration_ = 0;
    }

    // Marks the idle state as entered.
    void CountEntry() { times_entered_++; }

    // Records that the system spent |duration| in this state before exiting.
    void RecordDuration(zx_duration_t duration) {
        total_duration_ = zx_duration_add_duration(total_duration_, duration);
    }
private:
    const x86_idle_state_t* state_;
    uint64_t times_entered_;
    zx_duration_t total_duration_;
};

class X86IdleStates {
public:
    explicit X86IdleStates(const x86_idle_states_t* states);

    // Returns the list of states supported by the CPU, with the same
    // ordering constraints as documented for x86_idle_states_t.
    X86IdleState* States() { return states_; }
    const X86IdleState* ConstStates() const { return states_; }

    int NumStates() const { return num_states_; }

    // Picks an idle state to enter.
    X86IdleState* PickIdleState();
private:
    // TODO(jfsulliv): Replace with a std::array-like container
    X86IdleState states_[X86_MAX_CSTATES];
    int num_states_;
};

#endif // __cplusplus
