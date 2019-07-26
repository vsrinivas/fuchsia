/*
 * Copyright (c) 2018 The Fuchsia Authors
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "device.h"

#include <lib/async/time.h>  // for async_now()

#include "debug.h"

pthread_mutex_t irq_callback_lock;

async_dispatcher_t* default_dispatcher;

static void brcmf_timer_handler(async_dispatcher_t* dispatcher, async_task_t* task,
                                zx_status_t status) {
  if (status != ZX_OK) {
    return;
  }
  brcmf_timer_info_t* timer = containerof(task, brcmf_timer_info_t, task);
  timer->callback_function(timer->data);
  mtx_lock(&timer->lock);
  timer->scheduled = false;
  sync_completion_signal(&timer->finished);
  mtx_unlock(&timer->lock);
}

void brcmf_timer_init(brcmf_timer_info_t* timer, brcmf_timer_callback_t* callback, void* data) {
  memset(&timer->task.state, 0, sizeof(timer->task.state));
  timer->task.handler = brcmf_timer_handler;
  timer->data = data;
  timer->callback_function = callback;
  timer->finished = {};
  timer->scheduled = false;
  mtx_init(&timer->lock, mtx_plain);
}

void brcmf_timer_set(brcmf_timer_info_t* timer, zx_duration_t delay) {
  mtx_lock(&timer->lock);
  async_cancel_task(default_dispatcher, &timer->task);  // Make sure it's not scheduled
  timer->task.deadline = delay + async_now(default_dispatcher);
  timer->scheduled = true;
  sync_completion_reset(&timer->finished);
  async_post_task(default_dispatcher, &timer->task);
  mtx_unlock(&timer->lock);
}

void brcmf_timer_stop(brcmf_timer_info_t* timer) {
  mtx_lock(&timer->lock);
  if (!timer->scheduled) {
    mtx_unlock(&timer->lock);
    return;
  }
  zx_status_t result = async_cancel_task(default_dispatcher, &timer->task);
  mtx_unlock(&timer->lock);
  if (result != ZX_OK) {
    sync_completion_wait(&timer->finished, ZX_TIME_INFINITE);
  }
}
