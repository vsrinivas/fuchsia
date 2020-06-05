// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/reboot_watcher_manager.h"

#include <lib/fidl/llcpp/client.h>
#include <zircon/status.h>

#include "src/devices/lib/log/log.h"

using namespace llcpp::fuchsia::hardware::power::statecontrol;

RebootWatcherManager::RebootWatcherManager(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {}

bool RebootWatcherManager::ShouldNotifyWatchers() const {
  return reason_.has_value() && NumWatchers() > 0;
}

size_t RebootWatcherManager::NumWatchers() const { return num_bound_watchers_; }

void RebootWatcherManager::Register(
    zx::channel watcher,
    RebootMethodsWatcherRegister::Interface::RegisterCompleter::Sync _completer) {
  const size_t idx = watchers_.size();

  watchers_.push_back(Watcher{
      .client = fidl::Client<llcpp::fuchsia::hardware::power::statecontrol::RebootMethodsWatcher>(
          std::move(watcher), dispatcher_,
          [idx, this](fidl::UnboundReason, zx_status_t, zx::channel) {
            if (watchers_[idx].client_is_bound) {
              watchers_[idx].client_is_bound = false;
              --num_bound_watchers_;
            }
          }),
      .client_is_bound = true,
  });
  ++num_bound_watchers_;
}

void RebootWatcherManager::SetRebootReason(
    llcpp::fuchsia::hardware::power::statecontrol::RebootReason reason) {
  ZX_ASSERT(!reason_.has_value());
  reason_ = reason;
}

void RebootWatcherManager::NotifyAll(fit::closure watchdog, fit::closure on_last_reply) {
  if (!ShouldNotifyWatchers()) {
    return;
  }

  watchdog_task_ =
      std::make_unique<async::TaskClosure>([this, watchdog = std::move(watchdog)]() mutable {
        for (size_t i = 0; i < watchers_.size(); ++i) {
          UnbindWatcher(i);
        }
        watchdog();
      });

  const zx_status_t status =
      watchdog_task_->PostDelayed(dispatcher_, zx::sec(MAX_REBOOT_WATCHER_RESPONSE_TIME_SECONDS));

  if (status != ZX_OK) {
    LOGF(ERROR, "Failed to post timeout on watchers: %s", zx_status_get_string(status));
  }

  fidl::aligned<RebootReason> reason(reason_.value());

  auto done = std::make_shared<fit::closure>(std::move(on_last_reply));

  for (size_t i = 0; i < watchers_.size(); ++i) {
    if (!watchers_[i].client_is_bound) {
      continue;
    }

    watchers_[i].client->OnReboot(reason, [this, i, done]() mutable {
      UnbindWatcher(i);
      if (NumWatchers() == 0) {
        (*done)();
      }
    });
  }
}

void RebootWatcherManager::ExecuteWatchdog() {
  if (watchdog_task_ && watchdog_task_->is_pending()) {
    watchdog_task_->Cancel();

    const zx_status_t status = watchdog_task_->Post(dispatcher_);
    if (status != ZX_OK) {
      LOGF(ERROR, "Failed to repost watchdog: %s", zx_status_get_string(status));
    }
  }
}

void RebootWatcherManager::UnbindWatcher(size_t idx) {
  if (watchers_[idx].client_is_bound) {
    watchers_[idx].client.Unbind();
    watchers_[idx].client_is_bound = false;
    --num_bound_watchers_;
  }
}
