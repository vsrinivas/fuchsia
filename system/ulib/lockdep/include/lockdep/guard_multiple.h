// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

#include <fbl/integer_sequence.h>
#include <fbl/new.h>

#include <lockdep/common.h>
#include <lockdep/guard.h>
#include <lockdep/lock_class.h>

namespace lockdep {

// Guards simultaneous acquisitions of multiple locks of the same type.
template <size_t Size, typename LockType, typename Option = void>
class GuardMultiple {
    // Prevent passing nestable locks to GuardMultiple to avoid inconsistency
    // due to mixing external ordering and address ordering.
    static_assert((LockTraits<LockType>::Flags & LockFlagsNestable) == 0,
                  "Nestable locks cannot be used with GuardMultiple!");

public:
    GuardMultiple(GuardMultiple&&) = delete;
    GuardMultiple& operator=(GuardMultiple&&) = delete;

    GuardMultiple(const GuardMultiple&) = delete;
    GuardMultiple& operator=(const GuardMultiple&) = delete;

    // Locks the given set of locks, each belonging to the same lock class,
    // automatically ordering the arguments by address to preserve the
    // intra-class ordering invariant.
    template <typename Class, size_t Index,
              template <typename, typename, size_t> class... Locks>
    GuardMultiple(Locks<Class, LockType, Index>*... locks)
        : GuardMultiple{fbl::make_index_sequence<sizeof...(locks)>{}, locks...} {}

    ~GuardMultiple() {
        // Destroy union storage. Array elements are destroyed in reverse order.
        guard_storage_.~Storage();
    }

    // Releases all of the locks guarded by this instance.
    void Release() {
        for (size_t i = 0; i < Size; i++)
            guard_storage_.guards[i].Release();
    }

    // Returns true if all of the guards are holding acquired locks. In general
    // all of the guards should be in the same state.
    explicit operator bool() const {
        for (size_t i = 0; i < Size; i++) {
            if (!guard_storage_.guards[i])
                return false;
        }
        return true;
    }

private:
    // Quickly sorts an array of pointers in-place using insertion sort. When
    // Size is 2 this has been observed to instantiate as an inline swap.
    template <typename T>
    static void InsertionSortPointers(T* (&elements)[Size]) {
        size_t i = 1;
        while (i < Size) {
            T* x = elements[i];
            ssize_t j = i - 1;
            while (j >= 0 && elements[j] > x) {
                elements[j + 1] = elements[j];
                j--;
            }
            elements[j + 1] = x;
            i++;
        }
    }

    // Builds an array of Lock<LockType> pointers, sorts the array by ascending
    // address, and then aggregate initializes the union storage. Array elements
    // are constructed in order.
    template <size_t... Is, template <typename> class... Locks>
    GuardMultiple(fbl::index_sequence<Is...>, Locks<LockType>*... locks) {
        Lock<LockType>* lock_pointers[] = {locks...};
        InsertionSortPointers(lock_pointers);
        new (&guard_storage_) Storage{
            {{OrderedLock, lock_pointers[Is],
              reinterpret_cast<uintptr_t>(lock_pointers[Is])}...}};
    }

    // Storage type to permit late initialization of the underlying Guard
    // instances. Since Guard objects are not default construtible, using union
    // semantics allows the GuardMultiple constructor to delay initializing the
    // Guard array until after it has sorted the incoming lock pointers. This
    // type also leverages aggregate initialization since we can't use
    // std::array. Unfortunately, GCC only supports direct initialization of
    // C-style arrays through aggregate initialization so that is the only
    // option.
    struct Storage {
        Guard<LockType, Option> guards[Size];
    };

    union {
        Storage guard_storage_;
    };
};

} // namespace lockdep
