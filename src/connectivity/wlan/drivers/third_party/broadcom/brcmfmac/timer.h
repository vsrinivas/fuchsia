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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TIMER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TIMER_H_
#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/sync/completion.h>
#include <zircon/time.h>

#include <mutex>

// This is the function that timer users write to receive callbacks.
typedef void(brcmf_timer_callback_t)(void* data);

typedef enum brcmf_timer_type {
  BRCMF_TIMER_SINGLE_SHOT = 0,
  BRCMF_TIMER_PERIODIC,
} brcmf_timer_type_t;

typedef struct brcmf_timer_info {
  async_task_t task;
  async_dispatcher_t* dispatcher;
  void* data;
  brcmf_timer_callback_t* callback_function;
  bool scheduled = false;
  sync_completion_t finished;
  std::mutex lock;
  zx_duration_t delay;
  brcmf_timer_type_t type;
} brcmf_timer_info_t;

// Calling timer_set() and timer_stop() from within the timer handler is ok. But,
// calling timer_init() leaves the timer in an unknown state and might result in a
// crash
void brcmf_timer_init(brcmf_timer_info_t* timer, async_dispatcher_t* dispatcher,
                      brcmf_timer_callback_t* callback, void* data, bool periodic = false);

void brcmf_timer_set(brcmf_timer_info_t* timer, zx_duration_t delay);

void brcmf_timer_stop(brcmf_timer_info_t* timer);
#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TIMER_H_
