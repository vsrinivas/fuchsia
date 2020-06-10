// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/executor.h>
#include <object/memory_watchdog.h>

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

    case OomBehavior::kReboot:
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

      // It is important that we don't hang while trying to reboot.  Set a deadline by which we must
      // successfully reboot, else panic.
      //
      // How long should we wait?  If the system is OOMing chances are there are a lot of usermode
      // tasks so it make take a while for the shutdown threads to be scheduled.
      zx_time_t deadline = current_time() + ZX_SEC(10);
      platform_graceful_halt_helper(HALT_ACTION_REBOOT, ZirconCrashReason::Oom, deadline);
  }
}

void MemoryWatchdog::WorkerThread() {
  while (true) {
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
