// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_PREEMPT_DISABLER_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_PREEMPT_DISABLER_H_

#include <kernel/thread.h>

// AutoPreemptDisabler is a RAII helper that automatically manages disabling and
// re-enabling preemption. When the object goes out of scope, it automatically
// re-enables preemption if it had been previously disabled by the instance.
//
// Example usage:
//
// // Immediately disable preemption, then obtain the list_ lock and append an
// // element to the list.
// {
//   AutoPreemptDisabler preempt_disabler;
//   Guard<Mutex> guard{&lock_};
//   list_.push_back(ktl::move(element_uptr));
// }
//
//  // Reserve the option to disable preemption, but do not do so right now.
//  {
//     AutoPreemptDisabler preempt_disabler{AutoPreemptDisabler::Defer};
//     Guard<Mutex> guard{&lock_};
//
//     // Do some work.
//
//     if (predicate()) {
//       preempt_disabler.Disable();
//       // Do some more work with preemption disabled.
//     }
//  } // lock_ is released first, then (if predicate() was true), preemption is re-enabled.
//

class AutoPreemptDisabler {
 public:
  // Tag type to construct the AutoPreemptDisabler without preemption initially
  // disabled.
  enum DeferType { Defer };

  AutoPreemptDisabler() { Thread::Current::preemption_state().PreemptDisable(); }
  explicit AutoPreemptDisabler(DeferType) : disabled_{false} {}

  ~AutoPreemptDisabler() { Enable(); }

  AutoPreemptDisabler(const AutoPreemptDisabler&) = delete;
  AutoPreemptDisabler& operator=(const AutoPreemptDisabler&) = delete;
  AutoPreemptDisabler(AutoPreemptDisabler&&) = delete;
  AutoPreemptDisabler& operator=(AutoPreemptDisabler&&) = delete;

  // Disables preemption if it was not disabled by this instance already.
  void Disable() {
    if (!disabled_) {
      Thread::Current::preemption_state().PreemptDisable();
      disabled_ = true;
    }
  }

  // Enables preemption if it was previously disabled by this instance.
  void Enable() {
    if (disabled_) {
      Thread::Current::preemption_state().PreemptReenable();
      disabled_ = false;
    }
  }

 private:
  bool disabled_{true};
};

// AnnotatedAutoPreemptDisabler is an RAII helper which is almost identical in
// functionality to the AutoPreemptDisabler.  The main difference is that the
// Annotated version will automatically acquire/release the
// preempt_disabled_token, allowing it to be used in situations where static
// analysis demands proof that preemption has been disabled before a method can
// be called.
class TA_SCOPED_CAP AnnotatedAutoPreemptDisabler {
 public:
  AnnotatedAutoPreemptDisabler() TA_ACQ(preempt_disabled_token) {
    Thread::Current::preemption_state().PreemptDisableAnnotated();
  }

  ~AnnotatedAutoPreemptDisabler() TA_REL() { Enable(); }

  // Enables preemption if it was previously disabled by this instance.
  void Enable() TA_REL() {
    if (disabled_) {
      Thread::Current::preemption_state().PreemptReenableAnnotated();
      disabled_ = false;
    }
  }

  AnnotatedAutoPreemptDisabler(const AutoPreemptDisabler&) = delete;
  AnnotatedAutoPreemptDisabler& operator=(const AutoPreemptDisabler&) = delete;
  AnnotatedAutoPreemptDisabler(AutoPreemptDisabler&&) = delete;
  AnnotatedAutoPreemptDisabler& operator=(AutoPreemptDisabler&&) = delete;

 private:
  bool disabled_{true};
};

// AutoEagerReschedDisabler is a RAII helper that automatically manages
// disabling and re-enabling eager reschedules, including both local and remote
// CPUs. This type works the same as AutoPreemptDisable, except that it also
// prevents sending reschedule IPIs until eager reschedules are re-enabled.
class AutoEagerReschedDisabler {
 public:
  // Tag type to construct the AutoEagerReschedDisabler without eager
  // reschedules initially disabled.
  enum DeferType { Defer };

  AutoEagerReschedDisabler() { Thread::Current::preemption_state().EagerReschedDisable(); }
  explicit AutoEagerReschedDisabler(DeferType) : disabled_{false} {}

  ~AutoEagerReschedDisabler() { Enable(); }

  AutoEagerReschedDisabler(const AutoEagerReschedDisabler&) = delete;
  AutoEagerReschedDisabler& operator=(const AutoEagerReschedDisabler&) = delete;
  AutoEagerReschedDisabler(AutoEagerReschedDisabler&&) = delete;
  AutoEagerReschedDisabler& operator=(AutoEagerReschedDisabler&&) = delete;

  // Disables preemption if it was not disabled by this instance already.
  void Disable() {
    if (!disabled_) {
      Thread::Current::preemption_state().EagerReschedDisable();
      disabled_ = true;
    }
  }

  // Enables preemption if it was previously disabled by this instance.
  void Enable() {
    if (disabled_) {
      Thread::Current::preemption_state().EagerReschedReenable();
      disabled_ = false;
    }
  }

 private:
  bool disabled_{true};
};

// AnnotatedEagerReschedDsiabler is an RAII helper which is almost identical in
// functionality to the AutoEagerReschedDsiabler.  The main difference is that
// the Annotated version will automatically acquire/release the
// preempt_disabled_token, allowing it to be used in situations where static
// analysis demands proof that preemption has been disabled before a method can
// be called.
class TA_SCOPED_CAP AnnotatedAutoEagerReschedDisabler {
 public:
  AnnotatedAutoEagerReschedDisabler() TA_ACQ(preempt_disabled_token) {
    Thread::Current::preemption_state().EagerReschedDisableAnnotated();
  }
  ~AnnotatedAutoEagerReschedDisabler() TA_REL() { Enable(); }

  // Enables preemption if it was previously disabled by this instance.
  void Enable() TA_REL() {
    if (disabled_) {
      Thread::Current::preemption_state().EagerReschedReenableAnnotated();
      disabled_ = false;
    }
  }

  AnnotatedAutoEagerReschedDisabler(const AutoEagerReschedDisabler&) = delete;
  AnnotatedAutoEagerReschedDisabler& operator=(const AutoEagerReschedDisabler&) = delete;
  AnnotatedAutoEagerReschedDisabler(AutoEagerReschedDisabler&&) = delete;
  AnnotatedAutoEagerReschedDisabler& operator=(AutoEagerReschedDisabler&&) = delete;

 private:
  bool disabled_{true};
};

// AutoExpiringPreemptDisabler is an RAII helper that defers preemption of the
// current thread until either |max_deferral_duration| nanoseconds after
// preemption is requested or the object is destroyed, whichever comes first.
class AutoExpiringPreemptDisabler {
 public:
  explicit AutoExpiringPreemptDisabler(zx_duration_t max_deferral_duration)
      : should_clear_(
            Thread::Current::preemption_state().SetTimesliceExtension(max_deferral_duration)) {}

  ~AutoExpiringPreemptDisabler() {
    if (should_clear_) {
      Thread::Current::preemption_state().ClearTimesliceExtension();
    }
  }

  AutoExpiringPreemptDisabler(const AutoExpiringPreemptDisabler&) = delete;
  AutoExpiringPreemptDisabler& operator=(const AutoExpiringPreemptDisabler&) = delete;
  AutoExpiringPreemptDisabler(AutoExpiringPreemptDisabler&&) = delete;
  AutoExpiringPreemptDisabler& operator=(AutoExpiringPreemptDisabler&&) = delete;

 private:
  const bool should_clear_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_PREEMPT_DISABLER_H_
