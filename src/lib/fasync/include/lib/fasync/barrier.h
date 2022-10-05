// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_BARRIER_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_BARRIER_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <lib/fasync/bridge.h>
#include <lib/fasync/future.h>
#include <lib/fit/thread_safety.h>

#include <atomic>
#include <mutex>

namespace fasync {

// A barrier is utility class for monitoring pending futures and ensuring they have completed when
// |barrier.sync| completes. This class is used to mark futures with |barrier.wrap|, without
// changing their order, but allowing a caller to later invoke |sync| and ensure they have
// completed.
//
// EXAMPLE
//
//      // Issue tracked work, wrapped by the barrier.
//      fasync::barrier barrier;
//      auto work = fasync::make_future([] { do_work(); });
//      executor.schedule(work | fasync::wrap_with(barrier));
//
//      auto more_work = fasync::make_future([] { do_work_but_more(); });
//      executor.schedule(more_work | fasync::wrap_with(barrier));
//
//      // Ensure that all prior work completes, using the same barrier.
//      barrier.sync() | fasync::and_then([] {
//          // |work| and |more_work| have been completed.
//      });
//
// See documentation of |fasync::future| for more information.
class barrier final {
 public:
  barrier() {
    // Capture a new consumer and intentionally abandon its associated completer so that a future
    // chained onto the consumer using |future_or()| will become immediately runnable.
    fasync::bridge<fit::failed> bridge;
    prior_ = std::move(bridge.consumer);
  }

  ~barrier() = default;

  constexpr barrier(const barrier&) = delete;
  constexpr barrier(barrier&&) = delete;
  constexpr barrier& operator=(const barrier&) = delete;
  constexpr barrier& operator=(barrier&&) = delete;

  // Returns a new future which, after invoking the original |future|, may update sync() callers
  // if they are waiting for all prior work to complete.
  //
  // This method is thread-safe.
  template <typename F, ::fasync::internal::requires_conditions<is_future<F>> = true>
  decltype(auto) wrap(F&& future) {
    fasync::bridge<fit::failed> bridge;
    auto prior = swap_prior(std::move(bridge.consumer));

    // First, execute the originally provided future.
    //
    // Note that execution of this original future is not gated behind any interactions between
    // other calls to |sync()| or |wrap()|.
    return std::forward<F>(future) |
           fasync::then([prior = std::move(prior), completer = std::move(bridge.completer)](
                            fasync::context& context, auto&&... results) mutable {
             // Wait for all prior work to either terminate or be abandoned before terminating the
             // completer.
             //
             // This means that when |sync()| invokes |swap_prior()|, that caller receives a chain
             // of these future-bound completer objects from all prior invocations of |wrap()|.
             // When this chain completes, the sync future can complete too, since it implies that
             // all prior access to the barrier has completed.
             context.executor().schedule(
                 prior.future_or(fit::ok()) |
                 fasync::then([completer = std::move(completer)]() mutable {}));

             return std::make_tuple(std::forward<decltype(results)>(results)...);
           });
  }

  // Returns a future which completes after all previously wrapped work has completed.
  //
  // This method is thread-safe.
  fasync::try_future<fit::failed> sync() {
    // Swap the latest pending work with our own consumer; a subsequent request to sync should wait
    // on this one.
    fasync::bridge<fit::failed> bridge;
    fasync::consumer<fit::failed> prior = swap_prior(std::move(bridge.consumer));
    return prior.future_or(fit::ok()) |
           fasync::then([completer = std::move(bridge.completer)]() mutable {
             return fasync::make_ok_future();
           });
  }

 private:
  fasync::consumer<fit::failed> swap_prior(fasync::consumer<fit::failed> new_prior) {
    std::lock_guard<std::mutex> lock(mutex_);
    fasync::consumer<fit::failed> old_prior = std::move(prior_);
    prior_ = std::move(new_prior);
    return old_prior;
  }

  std::mutex mutex_;

  // Holds the consumption capability of the most recently wrapped future.
  fasync::consumer<fit::failed> prior_ FIT_GUARDED(mutex_);
};

}  // namespace fasync

LIB_FASYNC_CPP_VERSION_COMPAT_END

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_BARRIER_H_
