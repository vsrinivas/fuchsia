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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEVICE_H_

#include <lib/async-loop/loop.h>  // to start the worker thread
#include <lib/async/task.h>       // for async_post_task()
#include <lib/sync/completion.h>
#include <pthread.h>
#include <zircon/types.h>

#include <memory>

#include <ddk/device.h>

extern async_dispatcher_t* default_dispatcher;

// This is the function that timer users write to receive callbacks.
typedef void(brcmf_timer_callback_t)(void* data);

typedef struct brcmf_timer_info {
  async_task_t task;
  void* data;
  brcmf_timer_callback_t* callback_function;
  bool scheduled;
  sync_completion_t finished;
  mtx_t lock;
} brcmf_timer_info_t;

void brcmf_timer_init(brcmf_timer_info_t* timer, brcmf_timer_callback_t* callback, void* data);

void brcmf_timer_set(brcmf_timer_info_t* timer, zx_duration_t delay);

void brcmf_timer_stop(brcmf_timer_info_t* timer);

struct brcmf_bus;

struct brcmf_device {
  void* of_node;
  void* parent;
  std::unique_ptr<struct brcmf_bus> bus;
  zx_device_t* zxdev;
  zx_device_t* phy_zxdev;
  zx_device_t* if_zxdev;
};

static inline struct brcmf_bus* dev_to_bus(struct brcmf_device* dev) { return dev->bus.get(); }

// TODO(cphoenix): Wrap around whatever completion functions exist in PCIE and SDIO.
// TODO(cphoenix): To improve efficiency, analyze which spinlocks only need to protect small
// critical subsections of the completion functions. For those, bring back the individual spinlock.
// Note: This is a pthread_mutex_t instead of mtx_t because mtx_t doesn't implement recursive.
extern pthread_mutex_t irq_callback_lock;

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_DEVICE_H_
