// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_THREADING_MODEL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_THREADING_MODEL_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include <lib/fit/promise.h>
#include <lib/zx/time.h>
#include <zircon/compiler.h>

#include <memory>

namespace media::audio {

// ThreadToken and ScopedThreadToken are small (empty) objects which are intended to be used with
// the clang static thread analysis framework in order to express that some data may only be
// accessed from a single thread. By obtaining the capability represented by a threads token in an
// async operation submitted to that threads dispatcher, users may assert at compile time that they
// are only touching members from a single thread.
//
// This requires that the |async_dispatcher_t| backing any async waits is single threaded since this
// class does not do any actual locking.
struct __TA_CAPABILITY("role") ThreadToken {};

class __TA_SCOPED_CAPABILITY ScopedThreadToken {
 public:
  explicit ScopedThreadToken(const ThreadToken& token) __TA_ACQUIRE(token) {}
  ~ScopedThreadToken() __TA_RELEASE() {}
};

#define OBTAIN_EXECUTION_DOMAIN_TOKEN(_sym_name, _exe_domain) \
  ::media::audio::ScopedThreadToken _sym_name((_exe_domain)->token())

class ExecutionDomain {
 public:
  ExecutionDomain(async_dispatcher_t* dispatcher, fit::executor* executor)
      : dispatcher_(dispatcher), executor_(executor) {}

  // The async_dispatcher_t* for the loop running this domain.
  async_dispatcher_t* dispatcher() const { return dispatcher_; }

  // The fit::executor for the loop running this domain. Useful for scheduling fit::promises for
  // this domain.
  fit::executor* executor() const { return executor_; }

  // The |ThreadToken| that can be used to use static analysis to assert certain data members
  // are only accessed on this thread.
  //
  // Ex:
  //  class Foo {
  //   public:
  //    void TouchData() {
  //      async::PostTask(domain_->dispatcher(), [] {
  //        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, domain_);
  //        // This is now allowed since we've obtained the capability guarding |data_|.
  //        data_.Mutate();
  //      });
  //      // This would be a compile-time error as we have not acquired the capability guarding
  //      // |data_|.
  //      //
  //      // data_.Touch();
  //    }
  //   private:
  //    ExecutionDomain* domain_;
  //    Data data_ __TA_GUARDED(domain_->token());
  //  };
  const ThreadToken& token() const { return token_; }

  // Convenience access to post a task to this domains dispatcher.
  //
  // Ex, we can write:
  //   threading_model().FidlDomain().PostTask([] {...});
  // Instead of:
  //   async::PostTask(threading_model().FidlDomain().dispatcher(), [] {...});
  zx_status_t PostTask(fit::closure task) const {
    return async::PostTask(dispatcher_, std::move(task));
  }
  zx_status_t PostDelayedTask(fit::closure task, zx::duration delay) const {
    return async::PostDelayedTask(dispatcher_, std::move(task), delay);
  }
  zx_status_t PostTaskForTime(fit::closure task, zx::time deadline) const {
    return async::PostTaskForTime(dispatcher_, std::move(task), deadline);
  }

  // Convenience access to schedule a task to this domains executor.
  //
  // Ex, we can write:
  //   threading_model().FidlDomain().ScheduleTask(...);
  // Instead of:
  //   threading_model().FidlDomain().executor().schedule_task(...);
  void ScheduleTask(fit::pending_task task) const { executor_->schedule_task(std::move(task)); }

 private:
  async_dispatcher_t* const dispatcher_;
  fit::executor* executor_;
  ThreadToken token_;
};

enum class MixStrategy {
  // All mixing will happen on the same message loop used to run FIDL services.
  kMixOnFidlThread,
  // All mixing will happen on a single thread that is distinct from the thread used to run the
  // FIDL services.
  kMixOnSingleThread,
  // A new message loop will be allocated for each and every call to |AcquireMixDomain|.
  kThreadPerMix,
};

class ThreadingModel {
 public:
  // Parameters which control the deadline profile used for mixing threads.
  //
  // Our deadline and period is 10 mSec and our capacity is 4.4 mSec. This means that we will
  // receive 4.4 mSec of CPU time every 10mSec, and that 4.4 mSec may be scheduled at any point
  // during that 10 mSec window.
  static constexpr zx::duration kMixProfileCapacity = zx::usec(4'400);
  static constexpr zx::duration kMixProfileDeadline = zx::usec(10'000);
  static constexpr zx::duration kMixProfilePeriod = zx::usec(10'000);

  // Creates a |ThreadingModel| with a provided |MixStrategy|, which configures the behavior of
  // |AcquireMixDomain|.
  //
  // See |MixStrategy| for more details on possible strategies.
  static std::unique_ptr<ThreadingModel> CreateWithMixStrategy(MixStrategy mix_strategy);

  virtual ~ThreadingModel() = default;

  // Returns the |ExecutionDomain| used to run the primary |fuchsia::media::AudioCore| FIDL
  // service. This domain will be valid for the lifetime of this object.
  //
  // This is a single-threaded dispatcher.
  virtual ExecutionDomain& FidlDomain() = 0;

  // Returns the |ExecutionDomain| used to run blocking IO. This domain will be valid for the
  // the lifetime of this object.
  //
  // This is a single-threaded dispatcher.
  virtual ExecutionDomain& IoDomain() = 0;

  // We use a unique_ptr with a custom deleter to allow implementations to customize how the
  // |ExecutionDomain| is vended to clients. For example, with the |kMixOnFidlThread| mix strategy,
  // the returned domain will just be a pointer to the FIDL domain with a no-op deleter (since the
  // pointer is not actually backed by a unique allocation). Conversely with the |kThreadPerMix|
  // strategy, a new thread/dispatcher will be allocated for each acquired domain. In this situation
  // the message loop will be free'd by the deleter.
  using OwnedDomainPtr = std::unique_ptr<ExecutionDomain, fit::function<void(ExecutionDomain*)>>;

  // Acquires an |ExecutionDomain| to use for mixing. The returned domain will live as long as the
  // returned pointer.
  //
  // It is implementation defined if tasks will still execute after the returned |OwnedDomainPtr| is
  // released; for shared dispatcher implementations these tasks will still run, while
  // implementations that provide a unique dispatcher may choose to immediately shutdown the loop in
  // response to the |OwnedDomainPtr| being released.
  //
  // This is a single-threaded dispatcher.
  virtual OwnedDomainPtr AcquireMixDomain() = 0;

  // Runs all the dispatchers. When the message loop backing |FidlDomain()| exits, the remaining
  // domains will all be shutdown.
  //
  // When this method returns, all threads will be joined and all dispatchers stopped.
  virtual void RunAndJoinAllThreads() = 0;

  // Shuts down all |ExecutionDomains| provided by this |ThreadingModel|, causing
  // |RunAndJoinAllThreads| to eventually return.
  //
  // This posts the quit operation to all message loops managed by this object, meaning all
  // currently runnable tasks in each loop will have an opportunity to run before the loop exits.
  virtual void Quit() = 0;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_THREADING_MODEL_H_
