// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>
#include <kernel/spinlock.h>
#include <magenta/state_observer.h>
#include <magenta/types.h>
#include <mxtl/intrusive_double_list.h>

class Handle;

class StateTracker {
public:
    explicit StateTracker(bool is_waitable) : is_waitable_(is_waitable) { }
    virtual ~StateTracker() { }

    // Add an observer.
    virtual mx_status_t AddObserver(StateObserver* observer) = 0;

    // Remove an observer (which must have been added).
    virtual mx_signals_state_t RemoveObserver(StateObserver* observer) = 0;

    // Called when observers of the handle's state (e.g., waits on the handle) should be
    // "cancelled", i.e., when a handle (for the object that owns this StateTracker) is being
    // destroyed or transferred.
    virtual void Cancel(Handle* handle) = 0;

    // Notify others of a change in state (possibly waking them). (Clearing satisfied signals or
    // setting satisfiable signals should not wake anyone.)
    virtual void UpdateState(mx_signals_t satisfied_clear_mask,
                             mx_signals_t satisfied_set_mask,
                             mx_signals_t satisfiable_clear_mask,
                             mx_signals_t satisfiable_set_mask) = 0;

    virtual mx_signals_state_t GetSignalsState() = 0;

    bool is_waitable() const { return is_waitable_; }

protected:
    const bool is_waitable_;
};

namespace internal {

struct NonIrqStateTrackerTraits {
    struct LockState {
        LockState()  { mutex_init(&lock_); }
        ~LockState() { mutex_destroy(&lock_); }
        mutex_t lock_;
    };

    struct AutoLock {
        AutoLock(LockState* state) : lock_(&state->lock_) {
            mutex_acquire(lock_);
        }

        ~AutoLock() { mutex_release(lock_); }
        mutex_t* lock_;
    };

    static constexpr bool SignalableFromIrq = false;
};

struct IrqStateTrackerTraits {
    struct LockState {
        LockState()  { spin_lock_init(&lock_); }
        spin_lock_t lock_;
    };

    struct AutoLock {
        AutoLock(LockState* state) : lock_(&state->lock_) {
            spin_lock_irqsave(lock_, irq_state_);
        }
        ~AutoLock() { spin_unlock_irqrestore(lock_, irq_state_); }

        spin_lock_t* lock_;
        spin_lock_saved_state_t irq_state_;
    };

    static constexpr bool SignalableFromIrq = true;
};

template <typename Traits>
class StateTrackerImpl : public StateTracker {
public:
    // Note: The initial state can also be set using SetInitialSignalsState() if the default
    // constructor must be used for some reason.
    StateTrackerImpl(bool is_waitable = true,
                     mx_signals_state_t signals_state = mx_signals_state_t{0u, 0u})
        : StateTracker(is_waitable),
          signals_state_(signals_state) { }

    StateTrackerImpl(const StateTrackerImpl& o) = delete;
    StateTrackerImpl& operator=(const StateTrackerImpl& o) = delete;

    // Set the initial signals state. This is an alternative to provide the initial signals state to
    // the constructor. This does no locking and does not notify anything.
    void set_initial_signals_state(mx_signals_state_t signals_state) {
        signals_state_ = signals_state;
    }

    // Add an observer.
    mx_status_t AddObserver(StateObserver* observer) final;

    // Remove an observer (which must have been added).
    mx_signals_state_t RemoveObserver(StateObserver* observer) final;

    // Called when observers of the handle's state (e.g., waits on the handle) should be
    // "cancelled", i.e., when a handle (for the object that owns this StateTracker) is being
    // destroyed or transferred.
    void Cancel(Handle* handle) final;

    // Notify others of a change in state (possibly waking them). (Clearing satisfied signals or
    // setting satisfiable signals should not wake anyone.)
    void UpdateState(mx_signals_t satisfied_clear_mask,
                     mx_signals_t satisfied_set_mask,
                     mx_signals_t satisfiable_clear_mask,
                     mx_signals_t satisfiable_set_mask) final;

    mx_signals_state_t GetSignalsState() final;

    void UpdateSatisfied(mx_signals_t clear_mask, mx_signals_t set_mask) {
        UpdateState(clear_mask, set_mask, 0u, 0u);
    }

protected:
    bool UpdateStateInternal(mx_signals_t satisfied_clear_mask,
                             mx_signals_t satisfied_set_mask,
                             mx_signals_t satisfiable_clear_mask,
                             mx_signals_t satisfiable_set_mask);

private:
    using LockState = typename Traits::LockState;
    using AutoLock  = typename Traits::AutoLock;

    LockState lock_;  // Protects the members below.

    // Active observers are elements in |observers_|.
    mxtl::DoublyLinkedList<StateObserver*, StateObserverListTraits> observers_;

    // mojo-style signaling.
    mx_signals_state_t signals_state_;
};

}  // namespace internal

using NonIrqStateTracker = internal::StateTrackerImpl<internal::NonIrqStateTrackerTraits>;

class IrqStateTracker : public internal::StateTrackerImpl<internal::IrqStateTrackerTraits> {
public:
    IrqStateTracker(bool is_waitable = true,
                    mx_signals_state_t signals_state = mx_signals_state_t{0u, 0u})
        : internal::StateTrackerImpl<internal::IrqStateTrackerTraits>(is_waitable, signals_state) {}

    IrqStateTracker(const IrqStateTracker& o) = delete;
    IrqStateTracker& operator=(const IrqStateTracker& o) = delete;

    bool UpdateStateFromIrq(mx_signals_t satisfied_clear_mask,
                            mx_signals_t satisfied_set_mask,
                            mx_signals_t satisfiable_clear_mask,
                            mx_signals_t satisfiable_set_mask) {
        return UpdateStateInternal(satisfied_clear_mask,
                                   satisfied_set_mask,
                                   satisfiable_clear_mask,
                                   satisfiable_set_mask);
    }

    bool UpdateSatisfiedFromIrq(mx_signals_t satisfied_clear_mask,
                                mx_signals_t satisfied_set_mask) {
        return UpdateStateInternal(satisfied_clear_mask, satisfied_set_mask, 0u, 0u);
    }
};
