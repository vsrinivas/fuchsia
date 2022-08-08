// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/lib/timer/cpp/include/wlan/drivers/timer/timer.h"

#include <lib/async/time.h>
#include <lib/ddk/debug.h>
#include <lib/sync/completion.h>

namespace wlan::drivers::timer {

Timer::Timer(async_dispatcher_t* dispatcher, Callback callback, void* context)
    : async_task_t{{ASYNC_STATE_INIT}, &Timer::Handler, 0},
      dispatcher_(dispatcher),
      callback_(callback),
      context_(context) {}

Timer::~Timer() {
  zx_status_t status = Stop();
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to stop timer during destruction");
  }
}

zx_status_t Timer::StartPeriodic(zx_duration_t interval) { return Start(interval, true); }

zx_status_t Timer::StartOneshot(zx_duration_t delay) { return Start(delay, false); }

zx_status_t Timer::Stop() {
  // Make sure Start/Stop cannot be called from multiple threads at once. Doing so would open up
  // for race conditions for the section below where we don't hold unlock handler_mutex_ and wait
  // for handler completion. std::scoped_lock locks both mutexes without deadlocks.
  std::scoped_lock locks(start_stop_mutex_, handler_mutex_);

  // Set is_periodic_ to false right away. This ensures that if Stop was called from the callback
  // of a periodic timer (where scheduled_ would be false) it will not re-arm again.
  is_periodic_ = false;
  if (!scheduled_) {
    return ZX_OK;
  }
  scheduled_ = false;
  // Attempt to cancel the task. If this succeeds there is no risk of the timer handler being called
  // and we don't need to wait for completion.
  zx_status_t status = async_cancel_task(dispatcher_, this);
  if (status == ZX_OK) {
    return status;
  }
  if (status != ZX_ERR_NOT_FOUND) {
    zxlogf(ERROR, "Failed to cancel task: %s", zx_status_get_string(status));
    return status;
  }
  // At this point we know the task is scheduled but we were not able to cancel it. This means
  // that the only remaining possibility is that the task is about to run. It has been removed
  // from the dispatcher task list but did not acquire the handler mutex yet. We know this because
  // scheduled_ was still true and we hold the lock. By setting scheduled_ to false above we prevent
  // the timer handler from calling the callback. It should short-circuit and signal the completion.
  handler_mutex_.unlock();

  status = sync_completion_wait(&finished_, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to wait for completion: %s", zx_status_get_string(status));
    return status;
  }
  return ZX_OK;
}

zx_status_t Timer::Start(zx_duration_t interval, bool periodic) {
  if (interval < 0) {
    // Negative intervals and delays don't really make sense.
    return ZX_ERR_INVALID_ARGS;
  }

  // Calculate the deadline at this point to make sure that we get as close to the requested
  // interval as possible. Acquiring the locks might block for a while, causing timer drift if we
  // calculate the deadline when posting the task.
  zx_time_t deadline = async_now(dispatcher_) + interval;

  // Make sure Start/Stop cannot be called from multiple threads at once. Doing so would open up
  // for race conditions for the section below where we have to unlock handler_mutex_ and wait for
  // handler completion. std::scoped_lock locks both mutexes without deadlocks.
  std::scoped_lock locks(start_stop_mutex_, handler_mutex_);
  if (scheduled_) {
    // If Start was called from the dispatcher thread and scheduled_ is true that means that the
    // user called Start at least twice in the same callback, so we can safely cancel the previous
    // task. If Start was called from another thread then the task has to be scheduled at this
    // point.
    scheduled_ = false;
    is_periodic_ = false;

    zx_status_t status = async_cancel_task(dispatcher_, this);
    if (status == ZX_ERR_NOT_FOUND) {
      // We encountered the situation where the dispatcher has taken the task out its queue but the
      // timer handler has not yet locked the mutex. We've set scheduled_ to false so once the timer
      // handler is allowed to run it should immediately signal the completion and return.
      handler_mutex_.unlock();
      status = sync_completion_wait(&finished_, ZX_TIME_INFINITE);
      if (status != ZX_OK) {
        zxlogf(ERROR, "Failed to wait for completion: %s", zx_status_get_string(status));
        return status;
      }
      handler_mutex_.lock();
    } else if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to cancel task: %s", zx_status_get_string(status));
      return status;
    }
  }
  // At this point the timer must have stopped. Schedule it again.
  this->deadline = deadline;

  sync_completion_reset(&finished_);
  zx_status_t status = async_post_task(dispatcher_, this);
  if (status != ZX_OK) {
    zxlogf(ERROR, "Failed to post task: %s", zx_status_get_string(status));
    return status;
  }

  // Only set these on success, otherwise a Stop might attempt to cancel a task that doesn't exist.
  scheduled_ = true;
  is_periodic_ = periodic;
  interval_ = interval;

  return ZX_OK;
}

void Timer::Handler(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
  auto timer = static_cast<Timer*>(task);

  if (status != ZX_OK) {
    if (status != ZX_ERR_CANCELED) {
      // ZX_ERR_CANCELED is common enough that we don't need to log it, other errors are unexpected.
      zxlogf(ERROR, "Timer task failed to run: %s", zx_status_get_string(status));
    }
    if (timer) {
      // Signal the completion here in case someone is waiting for it.
      sync_completion_signal(&timer->finished_);
    }
    return;
  }

  std::lock_guard lock(timer->handler_mutex_);
  if (!timer->scheduled_) {
    // Timer was stopped but the task could not be removed from the dispatcher. Signal completion
    // and return without calling the callback, effectively stopping the timer.
    sync_completion_signal(&timer->finished_);
    return;
  }
  // Set scheduled_ to false here so that Start and Stop calls in the callback don't attempt to
  // stop or wait for the timer to trigger.
  timer->scheduled_ = false;

  // We intentionally keep the mutex held here to prevent tricky race conditions. This is fine since
  // it's a recursive mutex. Calls to Start and Stop from the callback will still work while at the
  // same time preventing other threads from getting through at the wrong time.
  timer->callback_(timer->context_);

  if (!timer->scheduled_ && timer->is_periodic_) {
    // The periodic timer should only be restarted if scheduled_ is false, otherwise a new timer was
    // started in the callback and we don't want to delay that timer by re-arming it here.
    zx_status_t status = timer->Start(timer->interval_, timer->is_periodic_);
    if (status != ZX_OK) {
      zxlogf(ERROR, "Failed to re-arm periodic timer: %s", zx_status_get_string(status));
    }
  }
}

}  // namespace wlan::drivers::timer
