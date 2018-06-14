// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
// Copyright (c) 2012 Shantanu Gupta
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <assert.h>
#include <debug.h>
#include <kernel/atomic.h>
#include <kernel/thread.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/thread_annotations.h>

__BEGIN_CDECLS

#define MUTEX_MAGIC (0x6D757478) // 'mutx'

// Body of the mutex.
// The val field holds either 0 or a pointer to the thread_t holding the mutex.
// If one or more threads are blocking and queued up, MUTEX_FLAG_QUEUED is ORed in as well.
// NOTE: MUTEX_FLAG_QUEUED is only manipulated under the THREAD_LOCK.
typedef struct TA_CAP("mutex") mutex {
    uint32_t magic;
    uintptr_t val;
    wait_queue_t wait;
} mutex_t;

#define MUTEX_FLAG_QUEUED ((uintptr_t)1)

// accessors to extract the holder pointer from the val member
static inline uintptr_t mutex_val(const mutex_t* m) {
    static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "");
    return atomic_load_u64_relaxed((uint64_t*)&m->val);
}

static inline thread_t* mutex_holder(const mutex_t* m) {
    static_assert(sizeof(uintptr_t) == sizeof(uint64_t), "");
    return (thread_t*)(mutex_val(m) & ~MUTEX_FLAG_QUEUED);
}

#define MUTEX_INITIAL_VALUE(m)                      \
    {                                               \
        .magic = MUTEX_MAGIC,                       \
        .val = 0,                                   \
        .wait = WAIT_QUEUE_INITIAL_VALUE((m).wait), \
    }

// Rules for Mutexes:
// - Mutexes are only safe to use from thread context.
// - Mutexes are non-recursive.
void mutex_init(mutex_t* m);
void mutex_destroy(mutex_t* m);
void mutex_acquire(mutex_t* m) TA_ACQ(m);
void mutex_release(mutex_t* m) TA_REL(m);

// special version of the above with the thread lock held
void mutex_release_thread_locked(mutex_t* m, bool reschedule) TA_REL(m);

// does the current thread hold the mutex?
static inline bool is_mutex_held(const mutex_t* m) {
    return (mutex_holder(m) == get_current_thread());
}

__END_CDECLS

#ifdef __cplusplus

#include <lockdep/lock_policy.h>
#include <lockdep/lock_traits.h>

// Declares a fbl::Mutex member of the struct or class |containing_type|.
//
// Example usage:
//
// struct MyType {
//     DECLARE_MUTEX(MyType) lock;
// };
//
#define DECLARE_MUTEX(containing_type) \
    LOCK_DEP_INSTRUMENT(containing_type, fbl::Mutex)

// Declares a |lock_type| member of the struct or class |containing_type|.
//
// Example usage:
//
// struct MyType {
//     DECLARE_LOCK(MyType, LockType) lock;
// };
//
#define DECLARE_LOCK(containing_type, lock_type) \
    LOCK_DEP_INSTRUMENT(containing_type, lock_type)

// Declares a singleton fbl::Mutex with the name |name|.
//
// Example usage:
//
//  DECLARE_SINGLETON_MUTEX(MyGlobalLock [, LockFlags]);
//
#define DECLARE_SINGLETON_MUTEX(name, ...) \
    LOCK_DEP_SINGLETON_LOCK(name, fbl::Mutex, ##__VA_ARGS__)

// Declares a singleton |lock_type| with the name |name|.
//
// Example usage:
//
//  DECLARE_SINGLETON_LOCK(MyGlobalLock, LockType, [, LockFlags]);
//
#define DECLARE_SINGLETON_LOCK(name, lock_type, ...) \
    LOCK_DEP_SINGLETON_LOCK(name, lock_type, ##__VA_ARGS__)

// Forward declaration.
struct MutexPolicy;

namespace fbl {

// Forward declaration. In the kernel this header is included by fbl/mutext.h.
class Mutex;

// Configure Guard<fbl::Mutex> to use the following policy. This must be done
// in the same namespace as the mutex type.
LOCK_DEP_POLICY(Mutex, MutexPolicy);

} // namespace fbl

// Lock policy for acquiring an fbl::Mutex.
struct MutexPolicy {
    // No extra state required for mutexes.
    struct State {};

    // Basic acquire and release operations.
    template <typename LockType>
    static bool Acquire(LockType* lock, State*) TA_ACQ(lock) {
        lock->Acquire();
        return true;
    }
    template <typename LockType>
    static void Release(LockType* lock, State*) TA_REL(lock) {
        lock->Release();
    }

    // A enum tag that can be passed to Guard<fbl::Mutex>::Release(...) to
    // select the special-case release method below.
    enum SelectThreadLockHeld { ThreadLockHeld };

    // Specifies whether the special-case release method below should
    // reschedule.
    enum RescheduleOption : bool {
        NoReschedule = false,
        Reschedule = true,
    };

    // Releases the lock using the special mutex release operation. This
    // is selected by calling:
    //
    //  Guard<fbl::Mutex>::Release(ThreadLockHeld [, Reschedule | NoReschedule])
    //
    template <typename LockType>
    static void Release(LockType* lock, State*, SelectThreadLockHeld,
                        RescheduleOption reschedule = Reschedule)
        TA_NO_THREAD_SAFETY_ANALYSIS {
        mutex_release_thread_locked(lock->GetInternal(), reschedule);
    }
};

#endif // __cplusplus
