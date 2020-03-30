// Copyright (c) 2019 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/timer.h"

#include <lib/async/time.h>
#include <zircon/listnode.h>
#include <zircon/types.h>

#include "debug.h"

Timer::Timer(async_dispatcher_t* dispatcher, std::function<void()> callback, bool periodic)
    : dispatcher_(dispatcher),
      task_({}),
      callback_(callback),
      interval_(0),
      scheduled_(false),
      finished_({}) {
  task_.handler = TimerHandler;
  if (periodic) {
    type_ = BRCMF_TIMER_PERIODIC;
  } else {
    type_ = BRCMF_TIMER_SINGLE_SHOT;
  }
}

// Set timer with timeout @interval. If interval is 0, return without setting
// the timer (can be used to stop a periodic timer)
void Timer::Start(zx_duration_t interval) {
  lock_.lock();

  interval_ = interval;

  if (!interval_) {
    // One way to stop periodic timer
    lock_.unlock();
    return;
  }
  async_cancel_task(dispatcher_, &task_);  // Make sure it's not scheduled
  task_.deadline = interval_ + async_now(dispatcher_);
  scheduled_ = true;
  sync_completion_reset(&finished_);
  async_post_task(dispatcher_, &task_);

  lock_.unlock();
}

void Timer::Stop() {
  lock_.lock();
  interval_ = 0;
  if (!scheduled_) {
    lock_.unlock();
    return;
  }
  zx_status_t result = async_cancel_task(dispatcher_, &task_);
  lock_.unlock();
  if (result != ZX_OK) {
    // In case the handler task could not be cancelled, wait for up to the
    // timeout interval for the handler task to finish.
    sync_completion_wait(&finished_, interval_);
  }
}

void Timer::TimerHandler(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status) {
  Timer* timer = containerof(task, Timer, task_);

  // Mark scheduled as false to avoid a race condition if Stop() or Start()
  // gets called in the handler.
  timer->lock_.lock();
  timer->scheduled_ = false;
  if (status != ZX_OK) {
    // If the handler is called in with an error status, ensure the timer ends up in a
    // good state.
    timer->interval_ = 0;
    timer->lock_.unlock();
    return;
  }
  timer->lock_.unlock();

  // Execute the handler
  timer->callback_();

  // Indicate done (to release Stop())
  timer->lock_.lock();
  sync_completion_signal(&timer->finished_);
  timer->lock_.unlock();

  // Check and reset timer in case it is periodic
  if (timer->type_ == BRCMF_TIMER_SINGLE_SHOT) {
    return;
  }

  timer->Start(timer->interval_);
}
