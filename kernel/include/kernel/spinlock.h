// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <arch/arch_ops.h>
#include <arch/spinlock.h>
#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>

__BEGIN_CDECLS

/* returns true if |lock| is held by the current CPU;
 * interrupts should be disabled before calling */
static inline bool spin_lock_held(spin_lock_t* lock) {
    return arch_spin_lock_held(lock);
}

// interrupts should already be disabled
static inline void spin_lock(spin_lock_t* lock) TA_ACQ(lock) {
    DEBUG_ASSERT(arch_ints_disabled());
    DEBUG_ASSERT(!spin_lock_held(lock));
    arch_spin_lock(lock);
}

// Returns 0 on success, non-0 on failure
static inline int spin_trylock(spin_lock_t* lock) TA_TRY_ACQ(false, lock) {
    return arch_spin_trylock(lock);
}

// interrupts should already be disabled
static inline void spin_unlock(spin_lock_t* lock) TA_REL(lock) {
    arch_spin_unlock(lock);
}

static inline void spin_lock_init(spin_lock_t* lock) {
    arch_spin_lock_init(lock);
}

// which cpu currently holds the spin lock
// returns UINT_MAX if not held
static inline uint spin_lock_holder_cpu(spin_lock_t* lock) {
    return arch_spin_lock_holder_cpu(lock);
}

// spin lock irq save flags:

// Possible future flags:
// SPIN_LOCK_FLAG_PMR_MASK         = 0x000000ff
// SPIN_LOCK_FLAG_PREEMPTION       = 0x00000100
// SPIN_LOCK_FLAG_SET_PMR          = 0x00000200

// Generic flags
#define SPIN_LOCK_FLAG_INTERRUPTS ARCH_DEFAULT_SPIN_LOCK_FLAG_INTERRUPTS

// same as spin lock, but save disable and save interrupt state first
static inline void spin_lock_save(spin_lock_t* lock, spin_lock_saved_state_t* statep,
                                  spin_lock_save_flags_t flags) TA_ACQ(lock) {
    arch_interrupt_save(statep, flags);
    spin_lock(lock);
}

// restore interrupt state before unlocking
static inline void spin_unlock_restore(spin_lock_t* lock, spin_lock_saved_state_t old_state,
                                       spin_lock_save_flags_t flags) TA_REL(lock) {
    spin_unlock(lock);
    arch_interrupt_restore(old_state, flags);
}

// hand(ier) routines
#define spin_lock_irqsave(lock, statep) spin_lock_save(lock, &(statep), SPIN_LOCK_FLAG_INTERRUPTS)
#define spin_unlock_irqrestore(lock, statep) spin_unlock_restore(lock, statep, SPIN_LOCK_FLAG_INTERRUPTS)

__END_CDECLS

#ifdef __cplusplus

#include <lockdep/lock_policy.h>
#include <lockdep/lock_traits.h>

class TA_CAP("mutex") SpinLock {
public:
    SpinLock() { spin_lock_init(&spinlock_); }
    void Acquire() TA_ACQ() { spin_lock(&spinlock_); }
    bool TryAcquire() TA_TRY_ACQ(false) { return spin_trylock(&spinlock_); }
    void Release() TA_REL() { spin_unlock(&spinlock_); }
    bool IsHeld() { return spin_lock_held(&spinlock_); }

    void AcquireIrqSave(spin_lock_saved_state_t& state,
                        spin_lock_save_flags_t flags = SPIN_LOCK_FLAG_INTERRUPTS)
        TA_ACQ() {
        spin_lock_save(&spinlock_, &state, flags);
    }

    void ReleaseIrqRestore(spin_lock_saved_state_t state,
                           spin_lock_save_flags_t flags = SPIN_LOCK_FLAG_INTERRUPTS)
        TA_REL() {
        spin_unlock_restore(&spinlock_, state, flags);
    }

    spin_lock_t* GetInternal() TA_RET_CAP(spinlock_) { return &spinlock_; }

    // suppress default constructors
    SpinLock(const SpinLock& am) = delete;
    SpinLock& operator=(const SpinLock& am) = delete;
    SpinLock(SpinLock&& c) = delete;
    SpinLock& operator=(SpinLock&& c) = delete;

private:
    spin_lock_t spinlock_;
};

// Declares a SpinLock member of the struct or class |containing_type|
// with instrumentation for runtime lock validation.
//
// Example usage:
//
// struct MyType {
//     DECLARE_SPINLOCK(MyType) lock;
// };
//
#define DECLARE_SPINLOCK(containing_type) \
    LOCK_DEP_INSTRUMENT(containing_type, SpinLock)

// Declares a singleton SpinLock with the name |name|.
//
// Example usage:
//
//  DECLARE_SINGLETON_SPINLOCK(MyGlobalLock [, LockFlags]);
//
#define DECLARE_SINGLETON_SPINLOCK(name, ...) \
    LOCK_DEP_SINGLETON_LOCK(name, SpinLock, ##__VA_ARGS__)

//
// Configure lockdep flags and wrappers for SpinLock.
//

// Configure lockdep to check irq-safety rules for SpinLock.
LOCK_DEP_TRAITS(SpinLock, lockdep::LockFlagsIrqSafe);

// Configure lockdep to check irq-safety rules for spin_lock_t.
LOCK_DEP_TRAITS(spin_lock_t, lockdep::LockFlagsIrqSafe);

// Option tag for acquiring a SpinLock WITHOUT saving irq state.
struct NoIrqSave {};

// Option tag for acquiring a SpinLock WITH saving irq state.
struct IrqSave {};

// Option tag for try-acquiring a SpinLock WITHOUT saving irq state.
struct TryLockNoIrqSave {};

// Base type for spinlock policies that do not save irq state.
template <typename LockType>
struct NoIrqSavePolicy;

// Lock policy for acquiring a SpinLock WITHOUT saving irq state.
template <>
struct NoIrqSavePolicy<SpinLock> {
    // No extra state required when not saving irq state.
    struct State {};

    static bool Acquire(SpinLock* lock, State*) TA_ACQ(lock) {
        lock->Acquire();
        return true;
    }
    static void Release(SpinLock* lock, State*) TA_REL(lock) {
        lock->Release();
    }
};

// Configure Guard<SpinLock, NoIrqSave> to use the above policy to acquire and
// release a SpinLock.
LOCK_DEP_POLICY_OPTION(SpinLock, NoIrqSave, NoIrqSavePolicy<SpinLock>);

// Lock policy for acquiring a SpinLock WITHOUT saving irq state.
template <>
struct NoIrqSavePolicy<spin_lock_t> {
    // No extra state required when not saving irq state.
    struct State {};

    static bool Acquire(spin_lock_t* lock, State*) TA_ACQ(lock) {
        spin_lock(lock);
        return true;
    }
    static void Release(spin_lock_t* lock, State*) TA_REL(lock) {
        spin_unlock(lock);
    }
};

// Configure Guard<spin_lock_t, NoIrqSave> to use the above policy to acquire and
// release a spin_lock_t.
LOCK_DEP_POLICY_OPTION(spin_lock_t, NoIrqSave, NoIrqSavePolicy<spin_lock_t>);

// Base type for spinlock policies that save irq state.
template <typename LockType>
struct IrqSavePolicy;

// Lock policy for acquiring a SpinLock WITH saving irq state.
template <>
struct IrqSavePolicy<SpinLock> {
    // State and flags required to save irq state.
    struct State {
        // This constructor receives the extra arguments passed to Guard when
        // locking an instrumented SpinLock like this:
        //
        //     Guard<SpinLock, IrqSave> guard{&a_spin_lock, |flags|};
        //
        // The extra argument to Guard is optional because this constructor has
        // a default value.
        State(spin_lock_save_flags_t flags = SPIN_LOCK_FLAG_INTERRUPTS)
            : flags{flags} {}

        spin_lock_save_flags_t flags;
        spin_lock_saved_state_t state;
    };

    static bool Acquire(SpinLock* lock, State* state) TA_ACQ(lock) {
        lock->AcquireIrqSave(state->state, state->flags);
        return true;
    }
    static void Release(SpinLock* lock, State* state) TA_REL(lock) {
        lock->ReleaseIrqRestore(state->state, state->flags);
    }
};

// Configure Guard<SpinLock, IrqSave> to use the above policy to acquire and
// release a SpinLock.
LOCK_DEP_POLICY_OPTION(SpinLock, IrqSave, IrqSavePolicy<SpinLock>);

// Lock policy for acquiring a spin_lock_t WITH saving irq state.
template <>
struct IrqSavePolicy<spin_lock_t> {
    // State and flags required to save irq state.
    struct State {
        // This constructor receives the extra arguments passed to Guard when
        // locking an instrumented spin_lock_t like this:
        //
        //     Guard<spin_lock_t, IrqSave> guard{&a_spin_lock, |flags|};
        //
        // The extra argument to Guard is optional because this constructor has
        // a default value.
        State(spin_lock_save_flags_t flags = SPIN_LOCK_FLAG_INTERRUPTS)
            : flags{flags} {}

        spin_lock_save_flags_t flags;
        spin_lock_saved_state_t state;
    };

    static bool Acquire(spin_lock_t* lock, State* state) TA_ACQ(lock) {
        spin_lock_save(lock, &state->state, state->flags);
        return true;
    }
    static void Release(spin_lock_t* lock, State* state) TA_REL(lock) {
        spin_unlock_restore(lock, state->state, state->flags);
    }
};

// Configure Guard<SpinLock, IrqSave> to use the above policy to acquire and
// release a SpinLock.
LOCK_DEP_POLICY_OPTION(spin_lock_t, IrqSave, IrqSavePolicy<spin_lock_t>);

// Base type for spinlock policies that try-acquire without saving irq state.
template <typename LockType>
struct TryLockNoIrqSavePolicy;

// Lock policy for try-acquiring a SpinLock WITHOUT saving irq state.
template <>
struct TryLockNoIrqSavePolicy<SpinLock> {
    // No extra state required when not saving irq state.
    struct State {};

    static bool Acquire(SpinLock* lock, State*) TA_TRY_ACQ(true, lock) {
        const bool failed = lock->TryAcquire();
        return !failed; // Guard uses true to indicate success.
    }
    static void Release(SpinLock* lock, State*) TA_REL(lock) {
        lock->Release();
    }
};

// Configure Guard<SpinLock, TryLockNoIrqSave> to use the above policy to
// acquire and release a SpinLock.
LOCK_DEP_POLICY_OPTION(SpinLock, TryLockNoIrqSave,
                       TryLockNoIrqSavePolicy<SpinLock>);

// Lock policy for try-acquiring a spin_lock_t WITHOUT saving irq state.
template <>
struct TryLockNoIrqSavePolicy<spin_lock_t> {
    // No extra state required when not saving irq state.
    struct State {};

    static bool Acquire(spin_lock_t* lock, State*) TA_TRY_ACQ(true, lock) {
        const bool failed = spin_trylock(lock);
        return !failed; // Guard uses true to indicate success.
    }
    static void Release(spin_lock_t* lock, State*) TA_REL(lock) {
        spin_unlock(lock);
    }
};

// Configure Guard<spin_lock_t, TryLockNoIrqSave> to use the above policy to
// acquire and release a spin_lock_t.
LOCK_DEP_POLICY_OPTION(spin_lock_t, TryLockNoIrqSave,
                       TryLockNoIrqSavePolicy<spin_lock_t>);

#endif // ifdef __cplusplus
