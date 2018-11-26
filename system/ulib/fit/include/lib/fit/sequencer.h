// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_SEQUENCER_H_
#define LIB_FIT_SEQUENCER_H_

#include <assert.h>

#include <mutex>

#include "bridge.h"
#include "thread_safety.h"

namespace fit {

// A sequencer imposes a first-in-first-out sequential execution order onto a
// sequence of promises.  Each successively enqueued promise remains suspended
// until all previously enqueued promises complete or are abandoned.
//
// |fit::sequencer| is designed to be used either on its own or chained
// onto a promise using |fit::promise::wrap_with()|.
//
// EXAMPLE
//
//     // This wrapper type is intended to be applied to
//     // a sequence of promises so we store it in a variable.
//     fit::sequencer seq;
//
//     // This task consists of some amount of work that must be
//     // completed sequentially followed by other work that can
//     // happen in any order.  We use |wrap_with()| to wrap the
//     // sequential work with the sequencer.
//     fit::promise<> perform_complex_task() {
//         return fit::make_promise([] { /* do sequential work */ })
//             .then([] (fit::result<> result) { /* this will also be wrapped */ })
//             .wrap_with(seq)
//             .then([] (fit::result<> result) { /* do more work */ });
//     }
//
class sequencer final {
public:
    sequencer();
    ~sequencer();

    // Returns a new promise which will invoke |promise| after all previously
    // enqueued promises on this sequencer have completed or been abandoned.
    //
    // This method is thread-safe.
    template <typename Promise>
    decltype(auto) wrap(Promise promise) {
        assert(promise);

        fit::bridge<> bridge;
        fit::consumer<> prior = swap_prior(std::move(bridge.consumer()));
        return prior.promise_or(fit::ok())
            .then([promise = std::move(promise),
                   completer = std::move(bridge.completer())](
                      fit::context& context, fit::result<>) mutable {
                // This handler will run once the completer associated
                // with the |prior| promise is abandoned.  Once the promise
                // has finished, both the promise and completer will be
                // destroyed thereby causing the next promise chained onto
                // the |bridge|'s associated consumer to become runnable.
                return promise(context);
            });
    }

    sequencer(const sequencer&) = delete;
    sequencer(sequencer&&) = delete;
    sequencer& operator=(const sequencer&) = delete;
    sequencer& operator=(sequencer&&) = delete;

private:
    fit::consumer<> swap_prior(fit::consumer<> new_prior);

    std::mutex mutex_;

    // Holds the consumption capability of the most recently wrapped promise.
    fit::consumer<> prior_ FIT_GUARDED(mutex_);
};

} // namespace fit

#endif // LIB_FIT_SEQUENCER_H_
