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

#include <threads.h>

#include "debug.h"

pthread_mutex_t irq_callback_lock;

async_t* default_async;

struct brcmf_bus* dev_get_drvdata(struct brcmf_device* dev) {
    return dev->drvdata;
}

struct brcmfmac_platform_data* dev_get_platdata(struct brcmf_device* dev) {
    brcmf_err("dev_get_platdata was called, but I don't know how to do it.\n");
    return NULL;
}

void dev_set_drvdata(struct brcmf_device* dev, struct brcmf_bus* drvdata) {
    dev->drvdata = drvdata;
}

static void brcmf_timer_handler(async_t* async, async_task_t* task, zx_status_t status) {
    if (status != ZX_OK) {
        return;
    }
    brcmf_timer_info_t* timer = containerof(task, brcmf_timer_info_t, task);
    timer->callback_function(timer->data);
    mtx_lock(&timer->lock);
    timer->scheduled = false;
    completion_signal(&timer->finished);
    mtx_unlock(&timer->lock);
}

void brcmf_timer_init(brcmf_timer_info_t* timer, brcmf_timer_callback_t* callback) {
    memset(&timer->task.state, 0, sizeof(timer->task.state));
    timer->task.handler = brcmf_timer_handler;
    timer->callback_function = callback;
    timer->finished = COMPLETION_INIT;
    timer->scheduled = false;
    mtx_init(&timer->lock, mtx_plain);
}

void brcmf_timer_set(brcmf_timer_info_t* timer, zx_duration_t delay) {
    mtx_lock(&timer->lock);
    async_cancel_task(default_async, &timer->task); // Make sure it's not scheduled
    timer->task.deadline = delay + async_now(default_async);
    timer->scheduled = true;
    completion_reset(&timer->finished);
    async_post_task(default_async, &timer->task);
    mtx_unlock(&timer->lock);
}

void brcmf_timer_stop(brcmf_timer_info_t* timer) {
    mtx_lock(&timer->lock);
    if (!timer->scheduled) {
        mtx_unlock(&timer->lock);
        return;
    }
    zx_status_t result = async_cancel_task(default_async, &timer->task);
    mtx_unlock(&timer->lock);
    if (result != ZX_OK) {
        completion_wait(&timer->finished, ZX_TIME_INFINITE);
    }
}
