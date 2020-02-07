// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/pressure.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <sys/stat.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include "src/lib/fxl/logging.h"

namespace monitor {

Pressure::Pressure(bool watch_for_changes, sys::ComponentContext* context,
                   async_dispatcher_t* dispatcher)
    : provider_dispatcher_(dispatcher) {
  if (InitMemPressureEvents() != ZX_OK) {
    return;
  }

  if (context) {
    context->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }

  if (watch_for_changes) {
    loop_.StartThread("memory-pressure-loop");
    watch_task_.Post(loop_.dispatcher());
  }
}

zx_status_t Pressure::InitMemPressureEvents() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::channel::create returned " << zx_status_get_string(status);
    return status;
  }
  const char* root_job_svc = "/svc/fuchsia.boot.RootJobForInspect";
  status = fdio_service_connect(root_job_svc, remote.release());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "fdio_service_connect returned " << zx_status_get_string(status);
    return status;
  }

  zx::job root_job;
  status = fuchsia_boot_RootJobForInspectGet(local.get(), root_job.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "fuchsia_boot_RootJobForInspectGet returned " << zx_status_get_string(status);
    return status;
  }

  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL,
                               events_[Level::kCritical].reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_system_get_event [CRITICAL] returned " << zx_status_get_string(status);
    return status;
  }

  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING,
                               events_[Level::kWarning].reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_system_get_event [WARNING] returned " << zx_status_get_string(status);
    return status;
  }

  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL,
                               events_[Level::kNormal].reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_system_get_event [NORMAL] returned " << zx_status_get_string(status);
    return status;
  }

  for (size_t i = 0; i < Level::kNumLevels; i++) {
    wait_items_[i].handle = events_[i].get();
    wait_items_[i].waitfor = ZX_EVENT_SIGNALED;
    wait_items_[i].pending = 0;
  }

  return ZX_OK;
}

void Pressure::WatchForChanges() {
  WaitOnLevelChange();

  watch_task_.Post(loop_.dispatcher());
}

void Pressure::WaitOnLevelChange() {
  // Wait on all events the first time around.
  size_t num_wait_items = (level_ == Level::kNumLevels) ? Level::kNumLevels : Level::kNumLevels - 1;

  zx_status_t status = zx_object_wait_many(wait_items_.data(), num_wait_items, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_wait_many returned " << zx_status_get_string(status);
    return;
  }

  for (size_t i = 0; i < Level::kNumLevels; i++) {
    if (wait_items_[i].pending) {
      wait_items_[i].pending = 0;
      OnLevelChanged(wait_items_[i].handle);

      // Move the event currently asserted to the end of the array.
      // Wait on only the first |kNumLevels| - 1 items next time around.
      std::swap(wait_items_[i].handle, wait_items_[Level::kNumLevels - 1].handle);
      break;
    }
  }
}

void Pressure::OnLevelChanged(zx_handle_t handle) {
  Level old_level = level_;
  for (size_t i = 0; i < Level::kNumLevels; i++) {
    if (events_[i].get() == handle) {
      level_ = Level(i);
      break;
    }
  }

  FXL_LOG(INFO) << "Memory pressure level changed from " << kLevelNames[old_level] << " to "
                << kLevelNames[level_];
  if (provider_dispatcher_) {
    post_task_.Post(provider_dispatcher_);
  }
}

void Pressure::PostLevelChange() {
  Level level_to_send = level_;
  // TODO(rashaeqbal): Throttle notifications to prevent thrashing.
  for (auto& watcher : watchers_) {
    // Notify the watcher only if we received a response for the previous change.
    if (watcher.response_received) {
      NotifyWatcher(watcher, level_to_send);
    }
  }
}

void Pressure::NotifyWatcher(WatcherState& watcher, Level level) {
  watcher.level_sent = level;
  watcher.response_received = false;

  watcher.proxy->OnLevelChanged(ConvertLevel(level), [&watcher, this]() {
    watcher.response_received = true;
    Level current_level = level_;
    // The watcher might have missed a level change if it occurred before this callback. If the
    // level has changed, notify the watcher.
    if (watcher.level_sent != current_level) {
      async::PostTask(provider_dispatcher_, [&]() { NotifyWatcher(watcher, current_level); });
    }
  });
}

void Pressure::RegisterWatcher(fidl::InterfaceHandle<fuchsia::memorypressure::Watcher> watcher) {
  fuchsia::memorypressure::WatcherPtr watcher_proxy = watcher.Bind();
  fuchsia::memorypressure::Watcher* proxy_raw_ptr = watcher_proxy.get();
  watcher_proxy.set_error_handler(
      [this, proxy_raw_ptr](zx_status_t status) { ReleaseWatcher(proxy_raw_ptr); });

  Level current_level = level_;
  watchers_.push_back({std::move(watcher_proxy), current_level, false});

  // Return current level.
  NotifyWatcher(watchers_.back(), current_level);
}

void Pressure::ReleaseWatcher(fuchsia::memorypressure::Watcher* watcher) {
  auto predicate = [watcher](const auto& target) { return target.proxy.get() == watcher; };
  watchers_.erase(std::remove_if(watchers_.begin(), watchers_.end(), predicate));
}

fuchsia::memorypressure::Level Pressure::ConvertLevel(Level level) {
  switch (level) {
    case Level::kCritical:
      return fuchsia::memorypressure::Level::CRITICAL;
    case Level::kWarning:
      return fuchsia::memorypressure::Level::WARNING;
    case Level::kNormal:
    default:
      return fuchsia::memorypressure::Level::NORMAL;
  }
}

}  // namespace monitor
