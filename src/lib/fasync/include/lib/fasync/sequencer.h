// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_SEQUENCER_H_
#define SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_SEQUENCER_H_

#include <lib/fasync/internal/compiler.h>

LIB_FASYNC_CPP_VERSION_COMPAT_BEGIN

#include <assert.h>
#include <lib/fasync/bridge.h>
#include <lib/fit/thread_safety.h>

#include <mutex>

namespace fasync {

// |fasync::sequencer|
//
// A sequencer imposes a first-in-first-out sequential execution order onto a sequence of futures.
// Each successively enqueued future remains suspended until all previously enqueued futures
// complete or are abandoned.
//
// |fasync::sequencer| is designed to be used either on its own or chained onto a future using
// |fasync::future::wrap_with()|.
//
// EXAMPLE
//
//     // This wrapper type is intended to be applied to a sequence of futures so we store it in a
//     // variable.
//     fasync::sequencer seq;
//
//     // This task consists of some amount of work that must be completed sequentially followed by
//     // other work that can happen in any order. We use |wrap_with()| to wrap the sequential work
//     // with the sequencer.
//     fasync::future<> perform_complex_task() {
//         return fasync::make_future([] { /* Do sequential work. */ }) |
//             fasync::then([] (fit::result<fit::failed>& result) {
//                 /* This will also be wrapped. */
//             }) |
//             fasync::wrap_with(seq) |
//             fasync::then([] (fit::result<fit::failed>& result) { /* Do more work. */ });
//     }
//
class sequencer final {
 public:
  sequencer() {
    // Capture a new consumer and intentionally abandon its associated completer so that a future
    // chained onto the consumer using |future_or()| will become immediately runnable.
    fasync::bridge<fit::failed> bridge;
    prior_ = std::move(bridge.consumer);
  }

  ~sequencer() = default;

  // Returns a new future which will invoke |future| after all previously enqueued futures on this
  // sequencer have completed or been abandoned.
  //
  // This method is thread-safe.
  template <typename F>
  decltype(auto) wrap(F&& future) {
    fasync::bridge<fit::failed> bridge;
    fasync::consumer<fit::failed> prior = swap_prior(std::move(bridge.consumer));
    return prior.future_or(fit::ok()) |
           fasync::then([future = std::move(future), completer = std::move(bridge.completer)](
                            fasync::context& context) mutable {
             //  This handler will run once the completer associated with the |prior| future is
             //  abandoned. Once the future has finished, both the future and completer will be
             //  destroyed thereby causing the next future chained onto the |bridge|'s associated
             //  consumer to become runnable.
             return cpp20::invoke(future, context);
           });
  }

  constexpr sequencer(const sequencer&) = delete;
  constexpr sequencer(sequencer&&) = delete;
  constexpr sequencer& operator=(const sequencer&) = delete;
  constexpr sequencer& operator=(sequencer&&) = delete;

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

#endif  // SRC_LIB_FASYNC_INCLUDE_LIB_FASYNC_SEQUENCER_H_
