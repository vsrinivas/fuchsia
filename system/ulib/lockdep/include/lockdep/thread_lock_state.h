// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>

#include <fbl/intrusive_double_list.h>

#include <lockdep/common.h>
#include <lockdep/lock_class_state.h>

namespace lockdep {

// Linked list entry that tracks a lock acquired by a thread. Each thread
// maintains a local list of AcquiredLockEntry instances. AcquiredLockEntry is
// intented to be allocated on the stack as a member of a RAII type to manage
// the lifetime of the acquisition. Consequently, this type is move-only to
// permit moving the context to a different stack frame. However, an instance
// must only be manipulated by the thread that created it.
class AcquiredLockEntry : public fbl::DoublyLinkedListable<AcquiredLockEntry*> {
public:
    AcquiredLockEntry() = default;
    AcquiredLockEntry(LockClassId id, uintptr_t order)
        : id_{id}, order_{order} {}

    ~AcquiredLockEntry() {
        ZX_DEBUG_ASSERT(!InContainer());
    }

    AcquiredLockEntry(const AcquiredLockEntry&) = delete;
    AcquiredLockEntry& operator=(const AcquiredLockEntry&) = delete;

    AcquiredLockEntry(AcquiredLockEntry&& other) { *this = move(other); }
    AcquiredLockEntry& operator=(AcquiredLockEntry&& other) {
        if (this != &other) {
            ZX_ASSERT(!InContainer());

            if (other.InContainer())
                Replace(&other);

            id_ = other.id_;
            order_ = other.order_;

            other.id_ = kInvalidLockClassId;
            other.order_ = 0;
        }
        return *this;
    }

    LockClassId id() const { return id_; }
    uintptr_t order() const { return order_; }

private:
    friend class ThreadLockState;

    // Replaces the given entry in the list with this entry.
    void Replace(AcquiredLockEntry* target);

    LockClassId id_{kInvalidLockClassId};
    uintptr_t order_{0};
};

// Tracks the locks held by a thread and updates accounting during acquire and
// release operations.
class ThreadLockState {
public:
    // Returns the ThreadLockState instance for the current thread.
    static ThreadLockState* Get() {
        return SystemGetThreadLockState();
    }

    // Attempts to add the given lock class to the acquired lock list. Lock
    // ordering and other checks are performed here.
    void Acquire(AcquiredLockEntry* lock_entry) {
        if (LockClassState::IsTrackingDisabled(lock_entry->id()))
            return;

        if (LockClassState::IsReportingDisabled(lock_entry->id()))
            reporting_disabled_count_++;

        // Scans the acquired lock list and performs the following operations:
        //  1. Checks that the given lock class is not already in the list unless
        //     the lock class is nestable or address ordering is correctly applied.
        //  2. Checks that the given lock class is not in the dependency set for
        //     any lock class already in the list.
        //  3. Checks that irq-safe locks are not held when acquiring an irq-unsafe
        //     lock.
        //  4. Adds each lock class already in the list to the dependency set of the
        //     given lock class.
        last_result_ = LockResult::Success;
        for (AcquiredLockEntry& entry : acquired_locks_) {
            if (entry.id() == lock_entry->id()) {
                if (lock_entry->order() <= entry.order()) {
                    if (!LockClassState::IsNestable(lock_entry->id()) && lock_entry->order() == 0)
                        Report(lock_entry, &entry, LockResult::AlreadyAcquired);
                    else
                        Report(lock_entry, &entry, LockResult::InvalidNesting);
                }
            } else {
                const LockResult result =
                    LockClassState::AddLockClass(lock_entry->id(), entry.id());
                if (result == LockResult::Success) {
                    // A new edge has been added to the graph, trigger a loop
                    // detection pass.
                    TriggerLoopDetection();
                } else if (result == LockResult::MaxLockDependencies) {
                    // If the dependency set is full report error.
                    Report(lock_entry, &entry, result);
                } else /* if (result == LockResult::DependencyExists) */ {
                    // Nothing to do when there are no changes to the graph.
                }

                // The following tests only need to be run when a new edge is
                // added for this ordered pair of locks; when the edge already
                // exists these tests have been performed before.
                if (result == LockResult::Success) {
                    const bool entry_irqsafe = LockClassState::IsIrqSafe(entry.id());
                    const bool lock_entry_irqsafe = LockClassState::IsIrqSafe(lock_entry->id());
                    if (entry_irqsafe && !lock_entry_irqsafe)
                        Report(lock_entry, &entry, LockResult::InvalidIrqSafety);

                    if (LockClassState::HasLockClass(entry.id(), lock_entry->id()))
                        Report(lock_entry, &entry, LockResult::OutOfOrder);
                }
            }
        }

        if (!LockClassState::IsActiveListDisabled(lock_entry->id()))
            acquired_locks_.push_back(lock_entry);
    }

    // Removes the given lock entry from the acquired lock list.
    void Release(AcquiredLockEntry* entry) {
        if (LockClassState::IsTrackingDisabled(entry->id()))
            return;

        if (LockClassState::IsReportingDisabled(entry->id()))
            reporting_disabled_count_--;

        if (entry->InContainer())
            acquired_locks_.erase(*entry);
    }

    // Returns result of the last Acquire operation for testing.
    LockResult last_result() const { return last_result_; }

    bool reporting_disabled() const { return reporting_disabled_count_ > 0; }

private:
    friend ThreadLockState* SystemGetThreadLockState();
    friend void SystemInitThreadLockState(ThreadLockState*);
    friend void AcquiredLockEntry::Replace(AcquiredLockEntry*);

    ThreadLockState() = default;
    ~ThreadLockState() = default;
    ThreadLockState(const ThreadLockState&) = delete;
    void operator=(const ThreadLockState&) = delete;

    // Replaces the given orignal entry with the replacement entry. This permits
    // lock entries to be allocated on the stack and migrate between stack
    // frames if lock guards are moved or returned.
    //
    // The original entry must already be on the acquired locks list and the
    // replacement entry must not be on any list.
    void Replace(AcquiredLockEntry* original, AcquiredLockEntry* replacement) {
        acquired_locks_.replace(*original, replacement);
    }

    // Reports a detected lock violation using the system-defined runtime handler.
    void Report(AcquiredLockEntry* bad_entry, AcquiredLockEntry* conflicting_entry,
                LockResult result) {
        if ((result == LockResult::AlreadyAcquired ||
             result == LockResult::InvalidNesting) &&
            LockClassState::IsReAcquireFatal(bad_entry->id())) {
            SystemLockValidationFatal(bad_entry, this,
                                      __GET_CALLER(0),
                                      __GET_FRAME(0),
                                      LockResult::AlreadyAcquired);
        }

        if (!reporting_disabled()) {
            reporting_disabled_count_++;

            SystemLockValidationError(bad_entry, conflicting_entry, this,
                                      __GET_CALLER(0), __GET_FRAME(0), result);

            reporting_disabled_count_--;

            // Update the last result for testing.
            if (last_result_ == LockResult::Success)
                last_result_ = result;
        }
    }

    // Triggers a loop detection by the system-defined runtime handler.
    void TriggerLoopDetection() {
        if (!reporting_disabled()) {
            reporting_disabled_count_++;

            SystemTriggerLoopDetection();

            reporting_disabled_count_--;
        }
    }

    // Tracks the lock classes acquired by the current thread.
    fbl::DoublyLinkedList<AcquiredLockEntry*> acquired_locks_{};

    // Tracks the number of locks held that have the LockFlagsReportingDisabled
    // flag set. Reporting and loop detection are not triggered when this count
    // is greater than zero. This value is also incremented by one for the
    // duration of a report or loop detection trigger to prevent recursive calls
    // due to locks acquired by the system-defined runtime API.
    uint16_t reporting_disabled_count_{0};

    // Tracks the result of the last Acquire operation for testing.
    LockResult last_result_{LockResult::Success};
};

// Defined after ThreadLockState because of dependency on its methods.
inline void AcquiredLockEntry::Replace(AcquiredLockEntry* target) {
    ThreadLockState::Get()->Replace(target, this);
}

} // namespace lockdep
