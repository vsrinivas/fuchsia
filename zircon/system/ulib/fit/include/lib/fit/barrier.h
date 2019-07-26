// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_BARRIER_H_
#define LIB_FIT_BARRIER_H_

#include <assert.h>

#include <atomic>
#include <mutex>

#include "bridge.h"
#include "promise.h"
#include "thread_safety.h"

namespace fit {

// A barrier is utility class for monitoring pending promises and ensuring they have completed when
// |barrier.sync| completes. This class is used to mark promises with |barrier.wrap|, without
// changing their order, but allowing a caller to later invoke |sync| and ensure they have
// completed.
//
// EXAMPLE
//
//      // Issue tracked work, wrapped by the barrier.
//      fit::barrier barrier;
//      auto work = fit::make_promise([] { do_work(); });
//      executor.schedule_task(work.wrap_with(barrier));
//
//      auto more_work = fit::make_promise([] { do_work_but_more(); });
//      executor.schedule_task(more_work.wrap_with(barrier));
//
//      // Ensure that all prior work completes, using the same barrier.
//      barrier.sync().and_then([] {
//          // |work| and |more_work| have been completed.
//      });
//
// See documentation of |fit::promise| for more information.
class barrier final {
 public:
  barrier();
  ~barrier();

  barrier(const barrier&) = delete;
  barrier(barrier&&) = delete;
  barrier& operator=(const barrier&) = delete;
  barrier& operator=(barrier&&) = delete;

  // Returns a new promise which, after invoking the original |promise|, may update sync() callers
  // if they are waiting for all prior work to complete.
  //
  // This method is thread-safe.
  template <typename Promise>
  decltype(auto) wrap(Promise promise) {
    assert(promise);

    fit::bridge<> bridge;
    auto prior = swap_prior(std::move(bridge.consumer));

    // First, execute the originally provided promise.
    //
    // Note that execution of this original promise is not gated behind any interactions
    // between other calls to |sync()| or |wrap()|.
    return promise.then([prior = std::move(prior), completer = std::move(bridge.completer)](
                            fit::context& context, typename Promise::result_type& result) mutable {
      // Wait for all prior work to either terminate or be abandoned before terminating the
      // completer.
      //
      // This means that when |sync()| invokes |swap_prior()|, that caller receives a chain
      // of these promise-bound completer objects from all prior invocations of |wrap()|.
      // When this chain completes, the sync promise can complete too, since it implies
      // that all prior access to the barrier has completed.
      context.executor()->schedule_task(prior.promise_or(fit::ok()).then(
          [completer = std::move(completer)](const fit::result<>&) mutable { return; }));

      return result;
    });
  }

  // Returns a promise which completes after all previously wrapped work has completed.
  //
  // This method is thread-safe.
  fit::promise<void, void> sync() {
    // Swap the latest pending work with our own consumer; a subsequent request
    // to sync should wait on this one.
    fit::bridge<> bridge;
    fit::consumer<> prior = swap_prior(std::move(bridge.consumer));
    return prior.promise_or(fit::ok()).then(
        [completer = std::move(bridge.completer)](const fit::result<>&) mutable {
          return fit::make_ok_promise();
        });
  }

 private:
  fit::consumer<> swap_prior(fit::consumer<> new_prior);

  std::mutex mutex_;

  // Holds the consumption capability of the most recently wrapped promise.
  fit::consumer<> prior_ FIT_GUARDED(mutex_);
};

}  // namespace fit

#endif  // LIB_FIT_BARRIER_H_
