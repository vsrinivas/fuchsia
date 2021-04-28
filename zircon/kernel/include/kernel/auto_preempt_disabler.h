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

  ~AutoPreemptDisabler() {
    if (disabled_) {
      Thread::Current::preemption_state().PreemptReenable();
    }
  }

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

  ~AutoEagerReschedDisabler() {
    if (disabled_) {
      Thread::Current::preemption_state().EagerReschedReenable();
    }
  }

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

 private:
  bool disabled_{true};
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_PREEMPT_DISABLER_H_
