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

#include "bus.h"
#include "core.h"
#include "debug.h"

// This is the function that timer users write to receive callbacks.
typedef void(brcmf_timer_callback_t)(void* data);

typedef enum brcmf_timer_type {
  BRCMF_TIMER_SINGLE_SHOT = 1,
  BRCMF_TIMER_PERIODIC = 2,
} brcmf_timer_type_t;

class Timer {
 public:
  Timer(struct brcmf_pub* drvr, std::function<void()> callback, bool periodic);
  // If timer is active it will call the callback function, must wait/cancel timer
  ~Timer() { Stop(); }
  // To avoid accidentally creating multiple timers using the same callback/data
  Timer(const Timer&) = delete;
  Timer(Timer&& t) = delete;
  // @interval: Interval of time before timer triggers. Same interval used for periodic timers.
  void Start(zx_duration_t interval);
  void Stop();
  bool Stopped() { return !scheduled_; };

 private:
  static void TimerHandler(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status);
  // TimerHandler for simulation test framework.
  void TimerHandler();

  async_task_t task_;
  std::function<void()> callback_;
  brcmf_timer_type_t type_;

  std::mutex lock_;
  zx_duration_t interval_;
  bool scheduled_;
  sync_completion_t finished_;

  // Variables used for simulation test framework
  uint64_t event_id_;
  struct brcmf_pub* drvr_;
};

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_TIMER_H_
