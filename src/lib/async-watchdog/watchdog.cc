// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/async-watchdog/watchdog.h"

#include <lib/async/cpp/time.h>
#include <lib/backtrace-request/backtrace-request.h>
#include <lib/syslog/cpp/macros.h>

#include <mutex>

namespace async_watchdog {

WatchdogImpl::WatchdogImpl(std::string thread_name, uint64_t warning_interval_ms,
                           uint64_t timeout_ms, async_dispatcher_t* watchdog_dispatcher,
                           async_dispatcher_t* watched_thread_dispatcher,
                           fit::closure run_update_fn, fit::function<bool(void)> check_update_fn)
    : thread_name_(thread_name),
      warning_interval_(zx::msec(warning_interval_ms)),
      timeout_(zx::msec(timeout_ms)),
      watchdog_dispatcher_(watchdog_dispatcher),
      watched_thread_dispatcher_(watched_thread_dispatcher),
      run_update_fn_(std::move(run_update_fn)),
      check_update_fn_(std::move(check_update_fn)) {
  FX_DCHECK(timeout_ms >= warning_interval_ms);
  for (size_t i = 0; i < kPollingNum; i++) {
    post_update_tasks_.push_back(std::make_unique<PostUpdateTaskClosureMethod>(this));
  }
  last_update_timestamp_ = async::Now(watchdog_dispatcher_);
}

WatchdogImpl::~WatchdogImpl() {
  std::lock_guard<std::mutex> lock(mutex_);
  FX_DCHECK(!initialized_ || finalized_);
}

void WatchdogImpl::Initialize() {
  std::lock_guard<std::mutex> lock(mutex_);
  FX_DCHECK(!initialized_ && !finalized_);
  initialized_ = true;
  PostTasks();
}

void WatchdogImpl::Finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  FX_DCHECK(initialized_ && !finalized_);
  finalized_ = true;
  for (auto& post_update_task : post_update_tasks_) {
    post_update_task->Cancel();
  }
  run_update_task_.Cancel();
  handle_timer_task_.Cancel();
}

void WatchdogImpl::PostUpdateTask() { run_update_task_.Post(watchdog_dispatcher_); }

void WatchdogImpl::RunUpdate() {
  std::lock_guard<std::mutex> lock(mutex_);
  last_update_timestamp_ = async::Now(watchdog_dispatcher_);
  run_update_fn_();
}

void WatchdogImpl::HandleTimer() {
  if (!check_update_fn_()) {
    mutex_.lock();
    auto duration_since_last_response = async::Now(watchdog_dispatcher_) - last_update_timestamp_;
    mutex_.unlock();

    backtrace_request();

    FX_LOGS(WARNING) << "The watched thread is not responsive for " << warning_interval_.to_msecs()
                     << " ms. "
                     << "It has been " << duration_since_last_response.to_msecs()
                     << " ms since last response. "
                     << "Please see klog for backtrace of all threads.";

    if (duration_since_last_response >= timeout_) {
      FX_CHECK(false) << "Fatal: Watchdog has detected timeout for more than "
                      << timeout_.to_msecs() << " ms in " << thread_name_;
    }
  }

  PostTasks();
}

void WatchdogImpl::PostTasks() {
  for (size_t i = 0; i < kPollingNum; i++) {
    zx::duration delay = warning_interval_ / (kPollingNum + 1) * (i + 1);
    post_update_tasks_[i]->PostDelayed(watched_thread_dispatcher_, delay);
  }
  handle_timer_task_.PostDelayed(watchdog_dispatcher_, warning_interval_);
}

Watchdog::Watchdog(std::string thread_name, uint64_t warning_interval_ms, uint64_t timeout_ms,
                   async_dispatcher_t* watched_thread_dispatcher)
    : loop_(&kAsyncLoopConfigNeverAttachToThread) {
  loop_.StartThread();

  auto watched_thread_is_responding = std::make_shared<bool>(false);
  auto post_update = [watched_thread_is_responding]() { *watched_thread_is_responding = true; };
  auto check_update = [watched_thread_is_responding]() {
    auto result = *watched_thread_is_responding;
    *watched_thread_is_responding = false;
    return result;
  };

  watchdog_impl_ = std::make_unique<WatchdogImpl>(thread_name, warning_interval_ms, timeout_ms,
                                                  loop_.dispatcher(), watched_thread_dispatcher,
                                                  std::move(post_update), std::move(check_update));
  watchdog_impl_->Initialize();
}

Watchdog::~Watchdog() { watchdog_impl_->Finalize(); }

}  // namespace async_watchdog
