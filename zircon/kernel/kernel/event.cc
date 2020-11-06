// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

/**
 * @file
 * @brief  Event wait and signal functions for threads.
 * @defgroup event Events
 *
 * An event is a subclass of a wait queue.
 *
 * Threads wait for events, with optional timeouts.
 *
 * Events are "signaled", releasing waiting threads to continue.
 * Signals may be one-shot signals (Event::AUTOUNSIGNAL), in which
 * case one signal releases only one thread, at which point it is
 * automatically cleared. Otherwise, signals release all waiting threads
 * to continue immediately until the signal is manually cleared with
 * Event::Unsignal().
 *
 * @{
 */

#include "kernel/event.h"

#include <assert.h>
#include <debug.h>
#include <sys/types.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <kernel/spinlock.h>
#include <kernel/thread.h>
#include <kernel/thread_lock.h>

/**
 * @brief  Destruct an Event object.
 *
 * Event's resources are freed and it may no longer be used.
 * Will panic if there are any threads still waiting.
 */
Event::~Event() {
  DEBUG_ASSERT(magic_ == kMagic);

  magic_ = 0;
  result_ = INT_MAX;
  flags_ = Flags(0);
}

zx_status_t Event::WaitWorker(const Deadline& deadline, Interruptible interruptible,
                              uint signal_mask) {
  zx_status_t ret = ZX_OK;

  DEBUG_ASSERT(magic_ == kMagic);
  DEBUG_ASSERT(!arch_blocking_disallowed());

  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};

  if (result_ != kNotSignalled) {
    ret = result_;

    /* signaled, we're going to fall through */
    if (flags_ & Event::AUTOUNSIGNAL) {
      /* autounsignal flag lets one thread fall through before unsignaling */
      result_ = kNotSignalled;
    }
  } else {
    /* unsignaled, block here */
    ret = wait_.BlockEtc(deadline, signal_mask, ResourceOwnership::Normal, interruptible);
  }

  return ret;
}

void Event::SignalInternal(bool reschedule, zx_status_t wait_result) TA_REQ(thread_lock) {
  DEBUG_ASSERT(magic_ == kMagic);
  DEBUG_ASSERT(wait_result != kNotSignalled);

  if (result_ == kNotSignalled) {
    if (flags_ & Event::AUTOUNSIGNAL) {
      /* try to release one thread and leave unsignaled if successful */
      if (!wait_.WakeOne(reschedule, wait_result)) {
        /*
         * if we didn't actually find a thread to wake up, go to
         * signaled state and let the next call to Wait
         * unsignal the event.
         */
        result_ = wait_result;
      }
    } else {
      /* release all threads and remain signaled */
      result_ = wait_result;
      wait_.WakeAll(reschedule, wait_result);
    }
  }
}

/**
 * @brief  Signal an event
 *
 * Signals an event.  If Event::AUTOUNSIGNAL is set in the event
 * object's flags, only one waiting thread is allowed to proceed.  Otherwise,
 * all waiting threads are allowed to proceed until such time as
 * Event::Unsignal() is called.
 *
 * @param e           Event object
 * @param reschedule  If true, waiting thread(s) are executed immediately,
 *                    and the current thread resumes only after the
 *                    waiting threads have been satisfied. If false,
 *                    waiting threads are placed at the head of the run
 *                    queue.
 * @param wait_result What status a wait call will return to the
 *                    thread or threads that are woken up.
 */
void Event::SignalEtc(bool reschedule, zx_status_t wait_result) {
  Guard<SpinLock, IrqSave> guard{ThreadLock::Get()};
  SignalInternal(reschedule, wait_result);
}

/* same as above, but the thread lock must already be held */
void Event::SignalThreadLocked() {
  DEBUG_ASSERT(arch_ints_disabled());
  DEBUG_ASSERT(thread_lock.IsHeld());

  SignalInternal(false, ZX_OK);
}

/**
 * @brief  Clear the "signaled" property of an event
 *
 * Used mainly for event objects without the Event::AUTOUNSIGNAL
 * flag.  Once this function is called, threads that call Event::Wait()
 * functions will once again need to wait until the event object
 * is signaled.
 *
 * @param e  Event object
 *
 * @return  Returns ZX_OK on success.
 */
zx_status_t Event::Unsignal() {
  DEBUG_ASSERT(magic_ == kMagic);

  result_ = kNotSignalled;

  return ZX_OK;
}
