// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/debuglog.h>
#include <lib/zircon-internal/macros.h>

#include <object/executor.h>
#include <object/memory_watchdog.h>
#include <platform/halt_helper.h>
#include <vm/loan_sweeper.h>
#include <vm/scanner.h>

namespace {

const char* PressureLevelToString(MemoryWatchdog::PressureLevel level) {
  switch (level) {
    case MemoryWatchdog::PressureLevel::kOutOfMemory:
      return "OutOfMemory";
    case MemoryWatchdog::PressureLevel::kImminentOutOfMemory:
      return "ImminentOutOfMemory";
    case MemoryWatchdog::PressureLevel::kCritical:
      return "Critical";
    case MemoryWatchdog::PressureLevel::kWarning:
      return "Warning";
    case MemoryWatchdog::PressureLevel::kNormal:
      return "Normal";
    default:
      return "Unknown";
  }
}

bool IsDiagnosticLevel(MemoryWatchdog::PressureLevel level) {
  return level == MemoryWatchdog::PressureLevel::kImminentOutOfMemory;
}

void HandleOnOomReboot() {
  if (!TakeHaltToken()) {
    // We failed to acquire the token.  Someone else must have it.  That's OK.  We'll rely on them
    // to halt/reboot.  Nothing left for us to do but wait.
    printf("memory-pressure: halt/reboot already in progress; sleeping forever\n");
    Thread::Current::Sleep(ZX_TIME_INFINITE);
  }
  // We now have the halt token so we're committed.  To ensure we record the true cause of the
  // reboot, we must ensure nothing (aside from a panic) prevents us from halting with reason OOM.

  // We are out of or nearly out of memory so future attempts to allocate may fail.  From this
  // point on, avoid performing any allocation.  Establish a "no allocation allowed" scope to
  // detect (assert) if we attempt to allocate.
  ScopedMemoryAllocationDisabled allocation_disabled;

  const int kSleepSeconds = 8;
  printf("memory-pressure: pausing for %ds after OOM mem signal\n", kSleepSeconds);
  zx_status_t status = Thread::Current::SleepRelative(ZX_SEC(kSleepSeconds));
  if (status != ZX_OK) {
    printf("memory-pressure: sleep after OOM failed: %d\n", status);
  }
  printf("memory-pressure: rebooting due to OOM\n");

  // Tell the oom_tests host test that we are about to generate an OOM
  // crashlog to keep it happy.  Without these messages present in a
  // specific order in the log, the test will fail.
  printf("memory-pressure: stowing crashlog\nZIRCON REBOOT REASON (OOM)\n");

  // The debuglog could contain diagnostic messages that would assist in debugging the cause of
  // the OOM.  Shutdown debuglog before rebooting in order to flush any queued messages.
  //
  // It is important that we don't hang during this process so set a deadline for the debuglog
  // to shutdown.
  //
  // How long should we wait?  Shutting down the debuglog includes flushing any buffered
  // messages to the serial port (if present).  Writing to a serial port can be slow.  Assuming
  // we have a full debuglog buffer of 128KB, at 115200 bps, with 8-N-1, it will take roughly
  // 11.4 seconds to drain the buffer.  The timeout should be long enough to allow a full DLOG
  // buffer to be drained.
  zx_time_t deadline = current_time() + ZX_SEC(20);
  status = dlog_shutdown(deadline);
  if (status != ZX_OK) {
    // If `dlog_shutdown` failed, there's not much we can do besides print an error (which
    // probably won't make it out anyway since we've already called `dlog_shutdown`) and
    // continue on to `platform_halt`.
    printf("ERROR: dlog_shutdown failed: %d\n", status);
  }
  platform_halt(HALT_ACTION_REBOOT, ZirconCrashReason::Oom);
}

}  // namespace

fbl::RefPtr<EventDispatcher> MemoryWatchdog::GetMemPressureEvent(uint32_t kind) {
  switch (kind) {
    case ZX_SYSTEM_EVENT_OUT_OF_MEMORY:
      return mem_pressure_events_[PressureLevel::kOutOfMemory];
    case ZX_SYSTEM_EVENT_IMMINENT_OUT_OF_MEMORY:
      return mem_pressure_events_[PressureLevel::kImminentOutOfMemory];
    case ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL:
      return mem_pressure_events_[PressureLevel::kCritical];
    case ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING:
      return mem_pressure_events_[PressureLevel::kWarning];
    case ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL:
      return mem_pressure_events_[PressureLevel::kNormal];
    default:
      return nullptr;
  }
}

// Callback used with |pmm_init_reclamation|.
// This is a very minimal save idx and signal an event as we are called under the pmm lock and must
// avoid causing any additional allocations.
void MemoryWatchdog::AvailableStateUpdatedCallback(void* context, uint8_t idx) {
  MemoryWatchdog* watchdog = reinterpret_cast<MemoryWatchdog*>(context);
  watchdog->AvailableStateUpdate(idx);
}

void MemoryWatchdog::AvailableStateUpdate(uint8_t idx) {
  MemoryWatchdog::mem_event_idx_ = PressureLevel(idx);
  MemoryWatchdog::mem_state_signal_.Signal();
}

void MemoryWatchdog::EvictionTriggerCallback(Timer* timer, zx_time_t now, void* arg) {
  MemoryWatchdog* watchdog = reinterpret_cast<MemoryWatchdog*>(arg);
  watchdog->EvictionTrigger();
}

void MemoryWatchdog::EvictionTrigger() {
  // This runs from a timer interrupt context, as such we do not want to be performing synchronous
  // eviction and blocking some random thread. Therefore we use the asynchronous eviction trigger
  // that will cause the eviction thread to perform the actual eviction work.
  if (eviction_strategy_ == EvictionStrategy::Continuous) {
    pmm_evictor()->EnableContinuousEviction(min_free_target_, free_mem_target_,
                                            Evictor::EvictionLevel::OnlyOldest,
                                            Evictor::Output::Print);
  } else {
    pmm_evictor()->EvictOneShotAsynchronous(min_free_target_, free_mem_target_,
                                            Evictor::EvictionLevel::OnlyOldest,
                                            Evictor::Output::Print);
  }
}

// Helper called by the memory pressure thread when OOM state is entered.
void MemoryWatchdog::OnOom() {
  switch (gBootOptions->oom_behavior) {
    case OomBehavior::kJobKill:
      if (!executor_->GetRootJobDispatcher()->KillJobWithKillOnOOM()) {
        printf("memory-pressure: no alive job has a kill bit\n");
      }

      // Since killing is asynchronous, sleep for a short period for the system to quiesce. This
      // prevents us from rapidly killing more jobs than necessary. And if we don't find a
      // killable job, don't just spin since the next iteration probably won't find a one either.
      Thread::Current::SleepRelative(ZX_MSEC(500));
      break;

    case OomBehavior::kReboot:
      HandleOnOomReboot();
  }
}

bool MemoryWatchdog::IsSignalDue(PressureLevel idx, zx_time_t time_now) const {
  // We signal a memory state change immediately if any of these conditions are met:
  // 1) The current index is lower than the previous one signaled (i.e. available memory is lower
  // now), so that clients can act on the signal quickly.
  // 2) |hysteresis_seconds_| have elapsed since the last time we examined the state.
  return idx < prev_mem_event_idx_ ||
         zx_time_sub_time(time_now, prev_mem_state_eval_time_) >= hysteresis_seconds_;
}

bool MemoryWatchdog::IsEvictionRequired(PressureLevel idx) const {
  // Trigger asynchronous eviction if:
  // 1) the memory availability state is more critical than the previous one
  // AND
  // 2) we're configured to evict at that level.
  //
  // Do not trigger asynchronous eviction at:
  // 1) OOM level, since we perform synchronous eviction in that case in order to attempt a quick
  // recovery. Also, we're about to signal filesystems to shut down on OOM, after which eviction
  // will be a no-op anyway, since there will no longer be any pager-backed memory to evict.
  // 2) a diagnostic level, i.e. a level which does not trigger any memory reclamation and is only
  // intended for diagnostic purposes.
  return idx < prev_mem_event_idx_ && idx <= max_eviction_level_ &&
         idx != PressureLevel::kOutOfMemory && !IsDiagnosticLevel(idx);
}

bool MemoryWatchdog::IsLoanSweeperRequired(PressureLevel idx) const {
  return idx < prev_mem_event_idx_ && idx <= max_loan_sweep_level_ &&
         idx != PressureLevel::kOutOfMemory && !IsDiagnosticLevel(idx);
}

void MemoryWatchdog::WorkerThread() {
  while (true) {
    // If we've hit OOM level perform some immediate synchronous eviction to attempt to avoid OOM.
    if (mem_event_idx_ == PressureLevel::kOutOfMemory) {
      printf("memory-pressure: free memory is %zuMB, evicting pages to prevent OOM...\n",
             pmm_count_free_pages() * PAGE_SIZE / MB);
      // Keep trying to perform eviction for as long as we are evicting non-zero pages and we remain
      // in the out of memory state.
      bool first_sync_pass = true;
      while (mem_event_idx_ == PressureLevel::kOutOfMemory) {
        uint64_t freed_pages = 0;
        freed_pages += pmm_loan_sweeper()->SynchronousSweep(/*is_continuous_sweep=*/false, /*also_replace_recently_pinned=*/true);
        if (!first_sync_pass) {
          freed_pages += pmm_evictor()->EvictOneShotSynchronous(
              MB * 10, Evictor::EvictionLevel::IncludeNewest, Evictor::Output::Print);
        }
        if (!first_sync_pass && freed_pages == 0) {
          printf("memory-pressure: found no pages to evict or sweep\n");
          break;
        }
        first_sync_pass = false;
      }
      printf("memory-pressure: free memory after OOM eviction and loan sweeper is %zuMB\n",
             pmm_count_free_pages() * PAGE_SIZE / MB);
    }

    // Get a local copy of the atomic. It's possible by the time we read this that we've already
    // exited the last observed state, but that's fine as we don't necessarily need to signal every
    // transient state.
    PressureLevel idx = mem_event_idx_;

    auto time_now = current_time();

    if (IsSignalDue(idx, time_now)) {
      printf("memory-pressure: memory availability state - %s\n", PressureLevelToString(idx));

      if (IsLoanSweeperRequired(idx)) {
        // Sweep for non-loaned pages we can replace with free loaned pages, to free up non-loaned
        // pages.
        pmm_loan_sweeper()->EnableContinuousSweep();
      } else {
        pmm_loan_sweeper()->DisableContinuousSweep();
      }

      if (IsEvictionRequired(idx)) {
        // Clear any previous eviction trigger. Once Cancel completes we know that we will not race
        // with the callback and are free to update the targets. Cancel will return true if the
        // timer was canceled before it was scheduled on a cpu, i.e. an eviction was outstanding.
        bool eviction_was_outstanding = eviction_trigger_.Cancel();

        const uint64_t free_mem = pmm_count_free_pages() * PAGE_SIZE;
        // Set the minimum amount to free as half the amount required to reach our desired free
        // memory level. This minimum ensures that even if the user reduces memory in reaction to
        // this signal we will always attempt to free a bit.
        // TODO: measure and fine tune this over time as user space evolves.
        min_free_target_ = free_mem < free_mem_target_ ? (free_mem_target_ - free_mem) / 2 : 0;

        // If eviction was outstanding when we canceled the eviction trigger, trigger eviction
        // immediately without any delay. We are here because of a rapid allocation spike which
        // caused the memory pressure to become more critical in a very short interval, so it might
        // be better to evict pages as soon as possible to try and counter the allocation spike.
        // Otherwise if eviction was not outstanding, trigger the eviction for slightly in the
        // future. Half the hysteresis time here is a balance between giving user space time to
        // release memory and the eviction running before the end of the hysteresis period.
        eviction_trigger_.SetOneshot(
            (eviction_was_outstanding ? time_now
                                      : zx_time_add_duration(time_now, hysteresis_seconds_ / 2)),
            EvictionTriggerCallback, this);
        printf("memory-pressure: set target memory to evict %zuMB (free memory is %zuMB)\n",
               min_free_target_ / MB, free_mem / MB);
      } else if (eviction_strategy_ == EvictionStrategy::Continuous && idx > max_eviction_level_) {
        // If we're out of the max configured eviction-eligible memory pressure level, disable
        // continuous eviction.

        // Cancel any outstanding eviction trigger, so that eviction is not accidentally enabled
        // *after* we disable it here.
        eviction_trigger_.Cancel();
        // Disable continuous eviction.
        pmm_evictor()->DisableContinuousEviction();
      }

      // Unsignal the last event that was signaled.
      zx_status_t status =
          mem_pressure_events_[prev_mem_event_idx_]->user_signal_self(ZX_EVENT_SIGNALED, 0);
      if (status != ZX_OK) {
        panic("memory-pressure: unsignal memory event %s failed: %d\n",
              PressureLevelToString(prev_mem_event_idx_), status);
      }

      // Signal event corresponding to the new memory state.
      status = mem_pressure_events_[idx]->user_signal_self(0, ZX_EVENT_SIGNALED);
      if (status != ZX_OK) {
        panic("memory-pressure: signal memory event %s failed: %d\n", PressureLevelToString(idx),
              status);
      }
      prev_mem_event_idx_ = idx;
      prev_mem_state_eval_time_ = time_now;

      // If we're below the out-of-memory watermark, trigger OOM behavior.
      if (idx == PressureLevel::kOutOfMemory) {
        OnOom();
      }

      // Wait for the memory state to change again.
      mem_state_signal_.Wait(Deadline::infinite());

    } else {
      prev_mem_state_eval_time_ = time_now;

      // We are ignoring this memory state transition. Wait for only |hysteresis_seconds_|, and then
      // re-evaluate the memory state. Otherwise we could remain stuck at the lower memory state if
      // mem_avail_state_updated_cb() is not invoked.
      mem_state_signal_.Wait(
          Deadline::no_slack(zx_time_add_duration(time_now, hysteresis_seconds_)));
    }
  }
}

void MemoryWatchdog::Init(Executor* executor) {
  DEBUG_ASSERT(executor_ == nullptr);

  executor_ = executor;

  for (uint8_t i = 0; i < PressureLevel::kNumLevels; i++) {
    auto level = PressureLevel(i);
    KernelHandle<EventDispatcher> event;
    zx_rights_t rights;
    zx_status_t status = EventDispatcher::Create(0, &event, &rights);
    if (status != ZX_OK) {
      panic("memory-pressure: create memory event %s failed: %d\n", PressureLevelToString(level),
            status);
    }
    mem_pressure_events_[i] = event.release();
  }

  if (gBootOptions->oom_enabled) {
    constexpr auto kNumWatermarks = PressureLevel::kNumLevels - 1;
    ktl::array<uint64_t, kNumWatermarks> mem_watermarks;

    // TODO(rashaeqbal): The watermarks chosen below are arbitrary. Tune them based on memory usage
    // patterns. Consider moving to percentages of total memory instead of absolute numbers - will
    // be easier to maintain across platforms.
    mem_watermarks[PressureLevel::kOutOfMemory] =
        (gBootOptions->oom_out_of_memory_threshold_mb) * MB;
    mem_watermarks[PressureLevel::kImminentOutOfMemory] =
        mem_watermarks[PressureLevel::kOutOfMemory] +
        (gBootOptions->oom_imminent_oom_delta_mb) * MB;
    mem_watermarks[PressureLevel::kCritical] = (gBootOptions->oom_critical_threshold_mb) * MB;
    mem_watermarks[PressureLevel::kWarning] = (gBootOptions->oom_warning_threshold_mb) * MB;

    uint64_t watermark_debounce = gBootOptions->oom_debounce_mb * MB;
    if (gBootOptions->oom_evict_at_warning) {
      max_eviction_level_ = PressureLevel::kWarning;
    }
    // Set our eviction target to be such that we try to get completely out of the max eviction
    // level, taking into account the debounce.
    free_mem_target_ = mem_watermarks[max_eviction_level_] + watermark_debounce;

    hysteresis_seconds_ = ZX_SEC(gBootOptions->oom_hysteresis_seconds);

    zx_status_t status =
        pmm_init_reclamation(&mem_watermarks[PressureLevel::kOutOfMemory], kNumWatermarks,
                             watermark_debounce, this, &AvailableStateUpdatedCallback);
    if (status != ZX_OK) {
      panic("memory-pressure: failed to initialize pmm reclamation: %d\n", status);
    }

    printf(
        "memory-pressure: memory watermarks - OutOfMemory: %zuMB, Critical: %zuMB, Warning: %zuMB, "
        "Debounce: %zuMB\n",
        mem_watermarks[PressureLevel::kOutOfMemory] / MB,
        mem_watermarks[PressureLevel::kCritical] / MB, mem_watermarks[PressureLevel::kWarning] / MB,
        watermark_debounce / MB);

    printf("memory-pressure: eviction trigger level - %s\n",
           PressureLevelToString(max_eviction_level_));

    if (gBootOptions->oom_evict_continuous) {
      eviction_strategy_ = EvictionStrategy::Continuous;
      printf("memory-pressure: eviction strategy - continuous\n");
    } else {
      eviction_strategy_ = EvictionStrategy::OneShot;
      printf("memory-pressure: eviction strategy - one-shot\n");
    }

    printf("memory-pressure: hysteresis interval - %ld seconds\n", hysteresis_seconds_ / ZX_SEC(1));

    printf("memory-pressure: ImminentOutOfMemory watermark - %zuMB\n",
           mem_watermarks[PressureLevel::kImminentOutOfMemory] / MB);

    auto memory_worker_thread = [](void* arg) -> int {
      MemoryWatchdog* watchdog = reinterpret_cast<MemoryWatchdog*>(arg);
      watchdog->WorkerThread();
    };
    auto thread =
        Thread::Create("memory-pressure-thread", memory_worker_thread, this, HIGHEST_PRIORITY);
    DEBUG_ASSERT(thread);
    thread->Detach();
    thread->Resume();
  }
}
