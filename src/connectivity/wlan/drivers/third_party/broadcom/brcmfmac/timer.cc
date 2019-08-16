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

static void brcmf_timer_handler(async_dispatcher_t* dispatcher, async_task_t* task,
                                zx_status_t status) {
  if (status != ZX_OK) {
    return;
  }
  brcmf_timer_info_t* timer = containerof(task, brcmf_timer_info_t, task);
  timer->callback_function(timer->data);
  timer->lock.lock();
  timer->scheduled = false;
  sync_completion_signal(&timer->finished);
  timer->lock.unlock();
}

void brcmf_timer_init(brcmf_timer_info_t* timer, async_dispatcher_t* dispatcher,
                      brcmf_timer_callback_t* callback, void* data) {
  memset(&timer->task.state, 0, sizeof(timer->task.state));
  timer->task.handler = brcmf_timer_handler;
  timer->dispatcher = dispatcher;
  timer->data = data;
  timer->callback_function = callback;
  timer->finished = {};
  timer->scheduled = false;
}

void brcmf_timer_set(brcmf_timer_info_t* timer, zx_duration_t delay) {
  timer->lock.lock();
  async_cancel_task(timer->dispatcher, &timer->task);  // Make sure it's not scheduled
  timer->task.deadline = delay + async_now(timer->dispatcher);
  timer->scheduled = true;
  sync_completion_reset(&timer->finished);
  async_post_task(timer->dispatcher, &timer->task);
  timer->lock.unlock();
}

void brcmf_timer_stop(brcmf_timer_info_t* timer) {
  timer->lock.lock();
  if (!timer->scheduled) {
    timer->lock.unlock();
    return;
  }
  zx_status_t result = async_cancel_task(timer->dispatcher, &timer->task);
  timer->lock.unlock();
  if (result != ZX_OK) {
    sync_completion_wait(&timer->finished, ZX_TIME_INFINITE);
  }
}
