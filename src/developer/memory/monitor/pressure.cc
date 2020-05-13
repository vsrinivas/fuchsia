// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/pressure.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <sys/stat.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/time.h>
#include <zircon/types.h>

namespace monitor {

// Called from the main dispatcher thread, which is also the provider_dispatcher_ thread.
// Sets up another thread "memory-pressure-loop", which waits on memory pressure level changes from
// the kernel. If a change is observed, this thread posts tasks to the main thread i.e. the
// provider_dispatcher_ thread (which also handles registration and deletion of watchers).
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

// Called from the main dispatcher thread.
zx_status_t Pressure::InitMemPressureEvents() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx::channel::create returned " << zx_status_get_string(status);
    return status;
  }
  const char* root_job_svc = "/svc/fuchsia.boot.RootJobForInspect";
  status = fdio_service_connect(root_job_svc, remote.release());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "fdio_service_connect returned " << zx_status_get_string(status);
    return status;
  }

  zx::job root_job;
  status = fuchsia_boot_RootJobForInspectGet(local.get(), root_job.reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "fuchsia_boot_RootJobForInspectGet returned " << zx_status_get_string(status);
    return status;
  }

  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL,
                               events_[Level::kCritical].reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx_system_get_event [CRITICAL] returned " << zx_status_get_string(status);
    return status;
  }

  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING,
                               events_[Level::kWarning].reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx_system_get_event [WARNING] returned " << zx_status_get_string(status);
    return status;
  }

  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL,
                               events_[Level::kNormal].reset_and_get_address());
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx_system_get_event [NORMAL] returned " << zx_status_get_string(status);
    return status;
  }

  for (size_t i = 0; i < Level::kNumLevels; i++) {
    wait_items_[i].handle = events_[i].get();
    wait_items_[i].waitfor = ZX_EVENT_SIGNALED;
    wait_items_[i].pending = 0;
  }

  return ZX_OK;
}

// Called from the memory-pressure-loop thread.
void Pressure::WatchForChanges() {
  WaitOnLevelChange();

  watch_task_.Post(loop_.dispatcher());
}

// Called from the memory-pressure-loop thread.
void Pressure::WaitOnLevelChange() {
  // Wait on all events the first time around.
  size_t num_wait_items = (level_ == Level::kNumLevels) ? Level::kNumLevels : Level::kNumLevels - 1;

  zx_status_t status = zx_object_wait_many(wait_items_.data(), num_wait_items, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "zx_object_wait_many returned " << zx_status_get_string(status);
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

// Called from the memory-pressure-loop thread.
void Pressure::OnLevelChanged(zx_handle_t handle) {
  Level old_level = level_;
  for (size_t i = 0; i < Level::kNumLevels; i++) {
    if (events_[i].get() == handle) {
      level_ = Level(i);
      break;
    }
  }

  FX_LOGS(INFO) << "Memory pressure level changed from " << kLevelNames[old_level] << " to "
                << kLevelNames[level_];
  if (provider_dispatcher_) {
    post_task_.Post(provider_dispatcher_);
  }
}

// Called from the provider_dispatcher_ thread.
void Pressure::PostLevelChange() {
  Level level_to_send = level_;
  // TODO(rashaeqbal): Throttle notifications to prevent thrashing.
  for (auto& watcher : watchers_) {
    // Notify the watcher only if we received a response for the previous level change, i.e. there
    // is no pending callback.
    if (!watcher->pending_callback) {
      watcher->pending_callback = true;
      NotifyWatcher(watcher.get(), level_to_send);
    }
  }
}

// Called from the provider_dispatcher_ thread.
void Pressure::NotifyWatcher(WatcherState* watcher, Level level) {
  // We should already have set |pending_callback| when the notification (call to NotifyWatcher())
  // was posted, to prevent removing |WatcherState| from |watchers_| in the error handler.
  ZX_DEBUG_ASSERT(watcher->pending_callback);

  // We should not be notifying a watcher if |needs_free| is set - indicating that a delayed free is
  // required. This can only happen if there was a pending callback when we tried to release the
  // watcher. No new notifications can be sent out while there is a pending callback. And when the
  // callback is invoked, the |WatcherState| is removed from the |watchers_| vector, so we won't
  // post any new notifications after that.
  ZX_DEBUG_ASSERT(!watcher->needs_free);

  watcher->level_sent = level;
  watcher->proxy->OnLevelChanged(ConvertLevel(level),
                                 [watcher, this]() { OnLevelChangedCallback(watcher); });
}

// Called from the provider_dispatcher_ thread.
void Pressure::OnLevelChangedCallback(WatcherState* watcher) {
  watcher->pending_callback = false;

  // The error handler invoked ReleaseWatcher(), but we could not free the |WatcherState| because of
  // this outstanding callback. It is safe to free the watcher now. There are no more outstanding
  // callbacks, and no new notifications (since a new notification is posted only if there is no
  // pending callback).
  if (watcher->needs_free) {
    ReleaseWatcher(watcher->proxy.get());
    return;
  }

  Level current_level = level_;
  // The watcher might have missed a level change if it occurred before this callback. If the
  // level has changed, notify the watcher.
  if (watcher->level_sent != current_level) {
    // Set |pending_callback| to true here before posting the NotifyWatcher() call. This ensures
    // that if ReleaseWatcher() is called (via the error handler) after we post the call, but before
    // we dispatch it, we don't access a freed |WatcherState*| in the NotifyWatcher() call.
    // ReleaseWatcher() will find |pending_callback| set, hence delay freeing the watcher and set
    // |needs_free| to true. NotifyWatcher() will operate on a valid |WatcherState*|, the next
    // callback will find |needs_free| set and free the watcher.
    watcher->pending_callback = true;
    async::PostTask(provider_dispatcher_,
                    [watcher, current_level, this]() { NotifyWatcher(watcher, current_level); });
  }
}

// Called from the provider_dispatcher_ thread.
void Pressure::RegisterWatcher(fidl::InterfaceHandle<fuchsia::memorypressure::Watcher> watcher) {
  fuchsia::memorypressure::WatcherPtr watcher_proxy = watcher.Bind();
  fuchsia::memorypressure::Watcher* proxy_raw_ptr = watcher_proxy.get();
  watcher_proxy.set_error_handler(
      [this, proxy_raw_ptr](zx_status_t status) { ReleaseWatcher(proxy_raw_ptr); });

  Level current_level = level_;
  watchers_.emplace_back(std::make_unique<WatcherState>(
      WatcherState{std::move(watcher_proxy), current_level, false, false}));

  // Set |pending_callback| and notify the current level.
  watchers_.back()->pending_callback = true;
  NotifyWatcher(watchers_.back().get(), current_level);
}

// Called from the provider_dispatcher_ thread.
void Pressure::ReleaseWatcher(fuchsia::memorypressure::Watcher* watcher) {
  auto predicate = [watcher](const auto& target) { return target->proxy.get() == watcher; };
  auto watcher_to_free = std::find_if(watchers_.begin(), watchers_.end(), predicate);
  if (watcher_to_free == watchers_.end()) {
    // Not found.
    return;
  }

  // There is a pending callback, which also means that the Watcher (client) holds a reference to
  // the |WatcherState| unique pointer (the callback captures a raw pointer - |WatcherState*|).
  // Freeing it now can lead to a use-after-free. Set |needs_free| to indicate that we need a
  // delayed free, when the pending callback is executed.
  //
  // NOTE: It is possible that a Watcher exits (closes its connection) and never invokes the
  // callback. In that case, we will never be able to free the corresponding |WatcherState|, which
  // is fine, since this is the only way we can safeguard against a use-after-free.
  if ((*watcher_to_free)->pending_callback) {
    (*watcher_to_free)->needs_free = true;
  } else {
    watchers_.erase(watcher_to_free);
  }
}

// Helper function. Has no thread affinity.
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
