// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_PREEMPT_DISABLER_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_PREEMPT_DISABLER_H_

#include <kernel/thread.h>

// AutoPreemptDisabler is a small RAII style helper which automatically manages
// disabling and re-enabling preemption via thread_preempt_(disable|reenable).
// It is offered as a partially specialized class which can either start by
// automatically disabling preemption when it is instantiated, or which allows
// preemption until a point in time where Disable is explicitly called by a
// user.  Either way, when the object goes out of scope, it automatically calls
// thread_preempt_reenable if preemption had been disabled during the life of
// the object.
//
// Note: In the version which begins with preemption enabled, multiple calls to
// Disable are idempotent.  In the version which begins with preemption
// disabled, there should never be any reason to explicitly call disable,
// therefore the method is omitted in order to deliberately fail to compile.
//
// Example usages:
//
// /* Immediately disable preemption, then obtain the list_ lock and append an
//  * element to the list.
//  */
// {
//   AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> ap_disabler;
//   Guard<Mutex> guard{&lock_};
//   list_.push_back(std::move(element_uptr));
// }
//
// /* Reserve the option to disable preemption, but do not do so right now.  If
//  * we decide to do so, however, we want to make certain that we do _not_ do so
//  * until after the lock is released.
//  */
//  {
//     AutoPreemptDisabler<APDInitialState::PREEMPT_ALLOWED> ap_disabler;
//     Guard<Mutex> guard{&lock_};
//
//     // Do some work
//
//     if (predicate()) {
//       ap_disabler.Disable();
//       // Do some more work with preemption disabled.
//     }
//  } // lock_ is released first, then (if predicate() was true), preemption is re-enabled.
//

// Enum which selects the version of the class to use.
enum class APDInitialState { PREEMPT_ALLOWED, PREEMPT_DISABLED };

// Fwd decl of the non-specialized class.
template <APDInitialState>
class AutoPreemptDisabler;

// The version which defers disabling until the user explicitly requests it.
template <>
class AutoPreemptDisabler<APDInitialState::PREEMPT_ALLOWED> {
 public:
  AutoPreemptDisabler() = default;

  ~AutoPreemptDisabler() {
    if (started_) {
      Thread::Current::PreemptReenable();
    }
  }

  void Disable() {
    if (!started_) {
      Thread::Current::PreemptDisable();
      started_ = true;
    }
  }

  // No move, no copy
  AutoPreemptDisabler(const AutoPreemptDisabler&) = delete;
  AutoPreemptDisabler(AutoPreemptDisabler&&) = delete;
  AutoPreemptDisabler& operator=(const AutoPreemptDisabler&) = delete;
  AutoPreemptDisabler& operator=(AutoPreemptDisabler&&) = delete;

 private:
  bool started_ = false;
};

// The version which automatically disables preemption from the start.
template <>
class AutoPreemptDisabler<APDInitialState::PREEMPT_DISABLED> {
 public:
  AutoPreemptDisabler() { Thread::Current::PreemptDisable(); }
  ~AutoPreemptDisabler() { Thread::Current::PreemptReenable(); }

  // No move, no copy
  AutoPreemptDisabler(const AutoPreemptDisabler&) = delete;
  AutoPreemptDisabler(AutoPreemptDisabler&&) = delete;
  AutoPreemptDisabler& operator=(const AutoPreemptDisabler&) = delete;
  AutoPreemptDisabler& operator=(AutoPreemptDisabler&&) = delete;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_AUTO_PREEMPT_DISABLER_H_
