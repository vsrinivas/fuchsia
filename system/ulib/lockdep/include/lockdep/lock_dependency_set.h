// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

#include <fbl/atomic.h>

#include <lockdep/common.h>

namespace lockdep {

// A lock-free, wait-free hash set that tracks the set of lock classes that have
// been acquired prior the lock class that owns the set. Each lock class
// maintains its own dependency set.
//
// Implementation note: This hash set makes use of relaxed atomic operations.
// This approach is appropriate because the only variables communicated between
// threads are the values of the atomic variables themselves, other loads and
// stores are not published. Additionally, sequential consistency within a
// thread is ensured due to control dependencies only on the atomic variables.
class LockDependencySet {
public:
    LockDependencySet() = default;
    LockDependencySet(const LockDependencySet&) = delete;
    void operator=(const LockDependencySet&) = delete;
    LockDependencySet(LockDependencySet&&) = delete;
    void operator=(LockDependencySet&&) = delete;

    // Checks the depedency hash set for the given lock class. This method may
    // safely race with AddLockClass(), converging on the correct answer by the
    // next check.
    bool HasLockClass(LockClassId id) const {
        for (size_t i = 0; i < kMaxLockDependencies; i++) {
            const auto& entry = GetEntry(id, i);
            const LockClassId entry_id = entry.load(fbl::memory_order_relaxed);
            if (entry_id == id)
                return true;
            else if (entry_id == kInvalidLockClassId)
                return false;
        }
        return false;
    }

    // Adds the given lock class id to the dependency set if not already present.
    // Updates the set using the following lock free approach:
    //   1. The dependency set is fixed size and all entries start out empty.
    //   2. New entries are added using open addressing with linear probing.
    //   3. An entry may only change from empty to holding a lock class id.
    //   4. To add an entry the set is probed linearly until either:
    //      a) The id to add appears in the set already.
    //      b) The first empty entry is found.
    //      c) The entire set has been probed; return max dependencies error.
    //   5. Attempt to compare-exchange the empty entry with the lock class id:
    //      a) If the update succeeds return success.
    //      b) If the update does not succeed but the winning value is the same
    //         lock class id return with success.
    //      c) If the update does not succeed and the winning value is a different
    //         lock class id proceed to the next entry and continue the probe from
    //         step #4.
    LockResult AddLockClass(LockClassId id) {
        for (size_t i = 0; i < kMaxLockDependencies; i++) {
            auto& entry = GetEntry(id, i);
            LockClassId entry_id = entry.load(fbl::memory_order_relaxed);
            if (entry_id == id)
                return LockResult::DependencyExists;
            while (entry_id == kInvalidLockClassId) {
                const bool result =
                    entry.compare_exchange_weak(&entry_id, id,
                                                fbl::memory_order_relaxed,
                                                fbl::memory_order_relaxed);
                if (result) {
                    return LockResult::Success;
                } else if (entry_id == id) {
                    return LockResult::DependencyExists;
                } else {
                    // Continue to find an unused slot, moving back to the outer
                    // loop because of the while loop condition.
                }
            }
        }

        return LockResult::MaxLockDependencies;
    }

    // Iterator type to traverse the set of populated lock classes. Entries
    // added after the iterator is created may or may not be returned, depending
    // on where they land in the hash set relative to the index member.
    struct Iterator {
        const LockDependencySet* set;
        size_t index;

        LockClassId operator*() const {
            // TODO(eieio): See if it makes sense to add a memory order param
            // to the iterator.
            return set->list_[index].load(fbl::memory_order_relaxed);
        }

        Iterator operator++() {
            while (index != kMaxLockDependencies) {
                index++;
                if (**this != kInvalidLockClassId)
                    break;
            }
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return set != other.set || index != other.index;
        }
    };

    // Iterator accessors.
    Iterator begin() const {
        Iterator iter{this, 0};
        if (*iter == kInvalidLockClassId)
            ++iter;
        return iter;
    }

    Iterator end() const { return {this, kMaxLockDependencies}; }

    // Clears the dependency set. This method is not used by the main algorithm
    // but may be useful for unit tests and benchmarking. Until lock sequence
    // memoization is implemented it is generally safe to call this method at
    // any time, resulting in dependency set being rebuilt at runtime. Once
    // lock sequence memoization is implemented it is necessary to clear the
    // memoization table whenever any dependency set is cleared so that the
    // dependency set can be rebuilt; failure to do so could result in missing
    // new lock violations.
    void clear() {
        for (size_t i = 0; i < kMaxLockDependencies; i++)
            list_[i].store(kInvalidLockClassId, fbl::memory_order_relaxed);
    }

private:
    // Returns a reference to an entry by computing a trivial hash of the given id
    // and a linear probe offset.
    fbl::atomic<LockClassId>& GetEntry(LockClassId id, size_t offset) {
        const size_t index = (id + offset) % kMaxLockDependencies;
        return list_[index];
    }
    const fbl::atomic<LockClassId>& GetEntry(LockClassId id, size_t offset) const {
        const size_t index = (id + offset) % kMaxLockDependencies;
        return list_[index];
    }

    // The list of atomic variables that make up the hash set. Initialized to
    // kInvalidLockClassId  (0).
    fbl::atomic<LockClassId> list_[kMaxLockDependencies]{};
};

} // namespace lockdep
