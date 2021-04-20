// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_TASK_RUNTIME_TIMERS_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_TASK_RUNTIME_TIMERS_H_

#include <platform.h>

#include <kernel/thread.h>
#include <object/thread_dispatcher.h>

class ContentionTimer {
 public:
  ContentionTimer(Thread* current_thread, zx_ticks_t start_ticks)
      : current_thread_(current_thread), start_ticks_(start_ticks) {}

  ContentionTimer(const ContentionTimer&) = delete;
  ContentionTimer(ContentionTimer&&) = delete;
  ContentionTimer& operator=(const ContentionTimer&) = delete;
  ContentionTimer& operator=(ContentionTimer&&) = delete;

  ~ContentionTimer() {
    ThreadDispatcher* user_thread = current_thread_->user_thread();
    if (likely(user_thread)) {
      user_thread->AddLockContentionTicks(current_ticks() - start_ticks_);
    }
  }

 private:
  Thread* const current_thread_;
  const zx_ticks_t start_ticks_;
};

class PageFaultTimer {
 public:
  PageFaultTimer(Thread* current_thread, zx_ticks_t start_ticks)
      : current_thread_(current_thread), start_ticks_(start_ticks) {}

  PageFaultTimer(const PageFaultTimer&) = delete;
  PageFaultTimer(PageFaultTimer&&) = delete;
  PageFaultTimer& operator=(const PageFaultTimer&) = delete;
  PageFaultTimer& operator=(PageFaultTimer&&) = delete;

  ~PageFaultTimer() {
    ThreadDispatcher* user_thread = current_thread_->user_thread();
    if (likely(user_thread)) {
      user_thread->AddPageFaultTicks(current_ticks() - start_ticks_);
    }
  }

 private:
  Thread* const current_thread_;
  const zx_ticks_t start_ticks_;
};

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_TASK_RUNTIME_TIMERS_H_
