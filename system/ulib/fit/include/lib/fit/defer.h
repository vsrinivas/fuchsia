// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <new>
#include <utility>

#include "traits_internal.h"

namespace fit {

// A move-only deferred action wrapper with RAII semantics.
// This class is not thread safe.
//
// The wrapper holds a function-like callable target with no arguments
// which it invokes when it goes out of scope unless canceled, called, or
// moved to a wrapper in a different scope.
//
// See |fit::defer()| for idiomatic usage.
template <typename T>
class deferred_action {
public:
    // Creates a deferred action without a pending target.
    deferred_action()
        : pending_(false) {}

    // Creates a deferred action with a pending target.
    explicit deferred_action(T target) {
        if (fit::internal::is_null(target)) {
            pending_ = false;
        } else {
            pending_ = true;
            new (&target_) T(std::move(target));
        }
    }

    // Creates a deferred action with a pending target moved from another
    // deferred action, leaving the other one without a pending target.
    deferred_action(deferred_action&& other) {
        move_from(std::move(other));
    }

    // Invokes and releases the deferred action's pending target (if any).
    ~deferred_action() {
        call();
    }

    // Returns true if the deferred action has a pending target.
    explicit operator bool() const {
        return pending_;
    }

    // Invokes and releases the deferred action's pending target (if any),
    // then move-assigns it from another deferred action, leaving the latter
    // one without a pending target.
    deferred_action& operator=(deferred_action&& other) {
        call();
        move_from(std::move(other));
        return *this;
    }

    // Invokes and releases the deferred action's pending target (if any).
    void call() {
        if (pending_) {
            // Move to a local to guard against re-entrance.
            T local_target = std::move(target_);
            pending_ = false;
            target_.~T();
            local_target();
        }
    }

    // Releases the deferred action's pending target (if any) without
    // invoking it.
    void cancel() {
        if (pending_) {
            pending_ = false;
            target_.~T();
        }
    }

    deferred_action(const deferred_action& other) = delete;
    deferred_action& operator=(const deferred_action& other) = delete;

private:
    void move_from(deferred_action&& other) {
        if (this == &other)
            return;
        pending_ = other.pending_;
        if (pending_) {
            new (&target_) T(std::move(other.target_));
            other.target_.~T();
            other.pending_ = false;
        }
    }

    bool pending_;

    // Storage for the target.
    // The target is only initialized when the call is pending.
    union {
        T target_;
    };
};

// Defers execution of a function-like callable target with no arguments
// until the value returned by this function goes out of scope unless canceled,
// called, or moved to a wrapper in a different scope.
//
// // This example prints "Hello..." then "Goodbye!".
// void test() {
//     auto d = fit::defer([]{ puts("Goodbye!"); });
//     puts("Hello...");
// }
//
// // This example prints nothing because the deferred action is canceled.
// void do_nothing() {
//     auto d = fit::defer([]{ puts("I'm not here."); });
//     d.cancel();
// }
//
// // This example shows how the deferred action can be reassigned assuming
// // the new target has the same type and the old one, in this case by
// // representing the target as a |fit::closure|.
// void reassign() {
//     auto d = fit::defer<fit::closure>([] { puts("This runs first."); });
//     d = fit::defer<fit::closure>([] { puts("This runs afterwards."); });
// }
template <typename T>
inline deferred_action<T> defer(T target) {
    return deferred_action<T>(std::move(target));
}

} // namespace fit
