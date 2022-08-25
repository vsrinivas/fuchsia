// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_TIMER_CPP_INCLUDE_WLAN_DRIVERS_TIMER_TIMER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_TIMER_CPP_INCLUDE_WLAN_DRIVERS_TIMER_TIMER_H_

#include <lib/async/dispatcher.h>
#include <lib/async/task.h>
#include <lib/sync/completion.h>
#include <zircon/status.h>

#include <functional>
#include <mutex>

namespace wlan::drivers::timer {

// A timer object that allows the user to start both oneshot and periodic timers. Destroying the
// timer object will first stop the timer. Note that StartPeriodic, StartOneshot and Stop are
// synchronous. Therefore the caller should be careful about holding locks that would prevent a
// timer trigger from completing when calling these methods. I.e. if the timer is in the middle of
// calling the callback and the callback is blocked on a lock held by the thread calling
// StartOneshot, StartPeriodic or Stop then those methods will never return.
//
// Calls to StartPeriodic, StartOneshot and Stop are mutually exclusive with eachother. If multiple
// threads call into these methods at the same time they will be processed sequentially and the
// order in which they are scheduled will determine the outcome.
class Timer : private async_task_t {
 public:
  using FunctionPtr = void (*)(void*);

  // Create a timer where `callback` will be called on `dispatcher`. The `context` parameter will
  // be provided in the call.
  Timer(async_dispatcher_t* dispatcher, FunctionPtr callback, void* context);
  // Create a timer where `callback` will be called on `dispatcher`.
  Timer(async_dispatcher_t* dispatcher, std::function<void()>&& callback);

  ~Timer();

  // Copying and moving cannot be done safely as the asynchronous tasks rely on the specific pointer
  // to the timer in question.
  Timer(const Timer&) = delete;
  Timer& operator=(const Timer&) = delete;

  // Start a periodic timer that will trigger at the specified interval. StartPeriodic is threadsafe
  // and can be called from the callback or elsewhere. Calling StartPeriodic on a running timer is
  // perfectly fine and will stop the existing timer and start it again with new parameters. If two
  // threads call StartPeriodic or StartOneshot at the same time they will be sequenced such that it
  // will appear as if one of them was made before the other. This means they will both succeed but
  // only the one sequenced last will have its parameters used for the timer. The timer will not
  // trigger twice. Negative intervals are not supported but a zero interval is OK (but probably not
  // advisable).
  zx_status_t StartPeriodic(zx_duration_t interval);
  // Start a oneshot timer that will trigger after the specified delay. StartOneshot is threadsafe
  // and can be called from the callback or elsewhere. Calling StartOneshot on a running timer is
  // perfectly fine and will stop the existing timer and start it again with new parameters. If two
  // threads call StartPeriodic or StartOneshot at the same time they will be sequenced such that it
  // will appear as if one of them was made before the other. This means they will both succeed but
  // only the one sequenced last will have its parameters used for the timer. The timer will not
  // trigger twice. Negative delays are not supported but zero delay is OK.
  zx_status_t StartOneshot(zx_duration_t delay);

  // Stop the timer if possible. If the timer has not yet triggered it will be stopped without any
  // calls to callback. If the timer is in the process of triggering there may still be a call to
  // callback but after Stop returns no further callbacks will be made. Stop is threadsafe and can
  // be called from a timer callback or anywhere else.
  zx_status_t Stop();

 private:
  zx_status_t Start(zx_duration_t interval, bool periodic);
  static void Handler(async_dispatcher_t* dispatcher, async_task_t* task, zx_status_t status);

  async_dispatcher_t* dispatcher_ = nullptr;

  // This mutex doesn't protect any data, it just prevents concurrent calls to Start and Stop. This
  // avoids race conditions as described in those methods.
  std::mutex start_stop_mutex_;
  // This is the mutex that protects the actual data. Unfortunately recursive mutexes do not seem to
  // be supported by thread analysis notation at this time.
  std::recursive_mutex handler_mutex_;
  bool scheduled_ = false;
  bool is_periodic_ = false;
  zx_duration_t interval_ = 0;
  sync_completion_t finished_;

  std::function<void()> callback_;
};

}  // namespace wlan::drivers::timer

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_LIB_TIMER_CPP_INCLUDE_WLAN_DRIVERS_TIMER_TIMER_H_
