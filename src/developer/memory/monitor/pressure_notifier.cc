// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/pressure_notifier.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

namespace monitor {

// |dispatcher| is the dispatcher associated with memory_monitor's main thread.
// The fuchsia::memorypressure::Provider service which the |PressureNotifier| class implements runs
// on this thread.
PressureNotifier::PressureNotifier(bool watch_for_changes, sys::ComponentContext* context,
                                   async_dispatcher_t* dispatcher)
    : provider_dispatcher_(dispatcher), context_(context), observer_(watch_for_changes, this) {
  if (context) {
    context->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }
}

void PressureNotifier::Notify() {
  if (provider_dispatcher_) {
    post_task_.Post(provider_dispatcher_);
  }
}

void PressureNotifier::PostLevelChange() {
  Level level_to_send = observer_.GetCurrentLevel();

  if (level_to_send == Level::kNormal) {
    // See comments about |observed_normal_level_| in the definition of |FileCrashReport()|.
    observed_normal_level_ = true;
  } else if (level_to_send == Level::kCritical && CanGenerateNewCrashReports()) {
    // File crash report before notifying watchers, so that we can capture the state *before*
    // watchers can respond to memory pressure, thereby changing the state that caused the memory
    // pressure in the first place.
    FileCrashReport();
  }

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

void PressureNotifier::NotifyWatcher(WatcherState* watcher, Level level) {
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

void PressureNotifier::OnLevelChangedCallback(WatcherState* watcher) {
  watcher->pending_callback = false;

  // The error handler invoked ReleaseWatcher(), but we could not free the |WatcherState| because of
  // this outstanding callback. It is safe to free the watcher now. There are no more outstanding
  // callbacks, and no new notifications (since a new notification is posted only if there is no
  // pending callback).
  if (watcher->needs_free) {
    ReleaseWatcher(watcher->proxy.get());
    return;
  }

  Level current_level = observer_.GetCurrentLevel();
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

void PressureNotifier::RegisterWatcher(
    fidl::InterfaceHandle<fuchsia::memorypressure::Watcher> watcher) {
  fuchsia::memorypressure::WatcherPtr watcher_proxy = watcher.Bind();
  fuchsia::memorypressure::Watcher* proxy_raw_ptr = watcher_proxy.get();
  watcher_proxy.set_error_handler(
      [this, proxy_raw_ptr](zx_status_t status) { ReleaseWatcher(proxy_raw_ptr); });

  Level current_level = observer_.GetCurrentLevel();
  watchers_.emplace_back(std::make_unique<WatcherState>(
      WatcherState{std::move(watcher_proxy), current_level, false, false}));

  // Set |pending_callback| and notify the current level.
  watchers_.back()->pending_callback = true;
  NotifyWatcher(watchers_.back().get(), current_level);
}

void PressureNotifier::ReleaseWatcher(fuchsia::memorypressure::Watcher* watcher) {
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

fuchsia::memorypressure::Level PressureNotifier::ConvertLevel(Level level) const {
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

bool PressureNotifier::CanGenerateNewCrashReports() {
  // Generate a new crash report only if any of these two conditions hold:
  // 1. |observed_normal_level_| is set to true, which indicates that a Normal level
  // was observed after the last Critical crash report.
  // 2. At least |crash_report_interval_| time has elapsed since the last crash report.
  //
  // This is done for two reasons:
  // 1) It helps limit the number of crash reports we generate.
  // 2) If the memory pressure changes to Critical again after going via Normal, we're
  // presumably observing a different memory usage pattern / use case, so it makes sense to
  // generate a new crash report. Instead if we're only observing Critical -> Warning ->
  // Critical transitions, we might be seeing the same memory usage pattern repeat.
  return (observed_normal_level_ ||
          zx::clock::get_monotonic() >= (prev_crash_report_time_ + crash_report_interval_));
}

void PressureNotifier::FileCrashReport() {
  if (context_ == nullptr) {
    return;
  }
  auto crash_reporter = context_->svc()->Connect<fuchsia::feedback::CrashReporter>();
  if (!crash_reporter) {
    return;
  }

  fuchsia::feedback::GenericCrashReport generic_report;
  generic_report.set_crash_signature("fuchsia-critical-memory-pressure");

  fuchsia::feedback::SpecificCrashReport specific_report;
  specific_report.set_generic(std::move(generic_report));

  fuchsia::feedback::CrashReport report;
  report.set_program_name("system");
  report.set_program_uptime(zx_clock_get_monotonic());
  report.set_specific_report(std::move(specific_report));

  crash_reporter->File(std::move(report),
                       [](fuchsia::feedback::CrashReporter_File_Result unused) {});

  prev_crash_report_time_ = zx::clock::get_monotonic();

  // Clear |observed_normal_level_| and wait for another normal level change to occur.
  observed_normal_level_ = false;
}

}  // namespace monitor
