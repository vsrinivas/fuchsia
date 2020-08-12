// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/debuglog.h>
#include <lib/zircon-internal/macros.h>

#include <object/executor.h>
#include <object/memory_watchdog.h>
#include <vm/scanner.h>

static const char* PressureLevelToString(MemoryWatchdog::PressureLevel level) {
  switch (level) {
    case MemoryWatchdog::PressureLevel::kOutOfMemory:
      return "OutOfMemory";
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

fbl::RefPtr<EventDispatcher> MemoryWatchdog::GetMemPressureEvent(uint32_t kind) {
  switch (kind) {
    case ZX_SYSTEM_EVENT_OUT_OF_MEMORY:
      return mem_pressure_events_[PressureLevel::kOutOfMemory];
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
  // that will cause the scanner thread to perform the actual eviction work.
  scanner_trigger_evict(min_free_target_, free_mem_target_, scanner::EvictionLevel::OnlyOldest);
}

// Helper called by the memory pressure thread when OOM state is entered.
void MemoryWatchdog::OnOom() {
  const char* oom_behavior_str = gCmdline.GetString("kernel.oom.behavior");

  // Default to reboot if not set or set to an unexpected value. See fxbug.dev/33429 for the product
  // details on when this path vs. the reboot should be used.
  enum class OomBehavior {
    kReboot,
    kJobKill,
  } oom_behavior = OomBehavior::kReboot;

  if (oom_behavior_str && strcmp(oom_behavior_str, "jobkill") == 0) {
    oom_behavior = OomBehavior::kJobKill;
  }

  switch (oom_behavior) {
    case OomBehavior::kJobKill:

      if (!executor_->GetRootJobDispatcher()->KillJobWithKillOnOOM()) {
        printf("memory-pressure: no alive job has a kill bit\n");
      }

      // Since killing is asynchronous, sleep for a short period for the system to quiesce. This
      // prevents us from rapidly killing more jobs than necessary. And if we don't find a
      // killable job, don't just spin since the next iteration probably won't find a one either.
      Thread::Current::SleepRelative(ZX_MSEC(500));
      break;

    case OomBehavior::kReboot: {
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

      // TODO(maniscalco): What prevents another thread from concurrently initiating a halt/reboot
      // of some kind (via RootJobObserver, syscall, etc.)?

      // The debuglog could contain diagnostic messages that would assist in debugging the cause of
      // the OOM.  Shutdown debuglog before rebooting in order to flush any queued messages.
      //
      // It is important that we don't hang during this process so set a deadline for the debuglog
      // to shutdown.
      //
      // TODO(fxbug.dev/55673): How long should we wait?  Shutting down the debuglog includes
      // flushing any buffered messages to the serial port (if present).  Writing to a serial port
      // can be slow. Assuming we have a full debuglog buffer of 128KB, at 115200 bps, with 8-N-1,
      // it will take roughly 11.4 seconds to drain the buffer.  The timeout should be long enough
      // to allow a full DLOG buffer to be drained.
      zx_time_t deadline = current_time() + ZX_SEC(20);
      status = dlog_shutdown(deadline);
      ASSERT_MSG(status == ZX_OK, "dlog_shutdown failed: %d\n", status);

      platform_halt(HALT_ACTION_REBOOT, ZirconCrashReason::Oom);
    }
  }
}

void MemoryWatchdog::WorkerThread() {
  while (true) {
    // If we've hit OOM level perform some immediate synchronous eviction to attempt to avoid OOM.
    if (mem_event_idx_ == PressureLevel::kOutOfMemory) {
      printf("memory-pressure: trying to prevent OOM by evicting pages...\n");
      list_node_t free_pages;
      list_initialize(&free_pages);
      // Keep trying to perform eviction for as long as we are evicting non-zero pages and we remain
      // in the out of memory state.
      while (mem_event_idx_ == PressureLevel::kOutOfMemory &&
             scanner_evict_pager_backed(MB * 10 / PAGE_SIZE, scanner::EvictionLevel::IncludeNewest,
                                        &free_pages) > 0) {
        pmm_free(&free_pages);
      }
    }

    // Get a local copy of the atomic. It's possible by the time we read this that we've already
    // exited the last observed state, but that's fine as we don't necessarily need to signal every
    // transient state.
    PressureLevel idx = mem_event_idx_;

    auto time_now = current_time();

    // We signal a memory state change immediately if:
    // 1) The current index is lower than the previous one signaled (i.e. available memory is lower
    // now), so that clients can act on the signal quickly.
    // 2) |kHysteresisSeconds| have elapsed since the last time we examined the state.
    if (idx < prev_mem_event_idx_ ||
        zx_time_sub_time(time_now, prev_mem_state_eval_time_) >= kHysteresisSeconds_) {
      printf("memory-pressure: memory availability state - %s\n", PressureLevelToString(idx));

      // Trigger eviction if the memory availability state is more critical than the previous one.
      // Currently we do not do any eviction at the kWarning level, only kCritical and below.
      if (idx < prev_mem_event_idx_ && idx <= PressureLevel::kCritical) {
        // Clear any previous eviction trigger. Once Cancel completes we know that we will not race
        // with the callback and are free to update the targets.
        eviction_trigger_.Cancel();
        const uint64_t free_mem = pmm_count_free_pages() * PAGE_SIZE;
        // Set the minimum amount to free as half the amount required to reach our desired free
        // memory level. This minimum ensures that even if the user reduces memory in reaction to
        // this signal we will always attempt to free a bit.
        // TODO: measure and fine tune this over time as user space evolves.
        min_free_target_ = free_mem < free_mem_target_ ? (free_mem_target_ - free_mem) / 2 : 0;
        // Trigger the eviction for slightly in the future. Half the hysteresis time here is a
        // balance between giving user space time to release memory and the eviction running before
        // the end of the hysteresis period.
        eviction_trigger_.Set(
            Deadline::no_slack(zx_time_add_duration(time_now, kHysteresisSeconds_ / 2)),
            EvictionTriggerCallback, this);
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
      if (idx == 0) {
        OnOom();
      }

      // Wait for the memory state to change again.
      mem_state_signal_.Wait(Deadline::infinite());

    } else {
      prev_mem_state_eval_time_ = time_now;

      // We are ignoring this memory state transition. Wait for only |kHysteresisSeconds|, and then
      // re-evaluate the memory state. Otherwise we could remain stuck at the lower memory state if
      // mem_avail_state_updated_cb() is not invoked.
      mem_state_signal_.Wait(
          Deadline::no_slack(zx_time_add_duration(time_now, kHysteresisSeconds_)));
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

  if (gCmdline.GetBool("kernel.oom.enable", true)) {
    constexpr auto kNumWatermarks = PressureLevel::kNumLevels - 1;
    ktl::array<uint64_t, kNumWatermarks> mem_watermarks;

    // TODO(rashaeqbal): The watermarks chosen below are arbitrary. Tune them based on memory usage
    // patterns. Consider moving to percentages of total memory instead of absolute numbers - will
    // be easier to maintain across platforms.
    mem_watermarks[PressureLevel::kOutOfMemory] =
        gCmdline.GetUInt64("kernel.oom.outofmemory-mb", 50) * MB;
    mem_watermarks[PressureLevel::kCritical] =
        gCmdline.GetUInt64("kernel.oom.critical-mb", 150) * MB;
    mem_watermarks[PressureLevel::kWarning] = gCmdline.GetUInt64("kernel.oom.warning-mb", 300) * MB;
    uint64_t watermark_debounce = gCmdline.GetUInt64("kernel.oom.debounce-mb", 1) * MB;

    // Set our eviction target to be such that we try to get completely out of the critical level,
    // taking into account the debounce.
    free_mem_target_ = mem_watermarks[PressureLevel::kCritical] + watermark_debounce;

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

    auto memory_worker_thread = [](void* arg) -> int {
      MemoryWatchdog* watchdog = reinterpret_cast<MemoryWatchdog*>(arg);
      watchdog->WorkerThread();
    };
    auto thread =
        Thread::Create("memory-pressure-thread", memory_worker_thread, this, HIGH_PRIORITY);
    DEBUG_ASSERT(thread);
    thread->Detach();
    thread->Resume();
  }
}
