// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MEMORY_WATCHDOG_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MEMORY_WATCHDOG_H_

#include <inttypes.h>
#include <lib/cmdline.h>
#include <lib/crashlog.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

#include <object/diagnostics.h>
#include <object/event_dispatcher.h>
#include <object/job_dispatcher.h>
#include <object/port_dispatcher.h>
#include <platform/crashlog.h>

class Executor;

class MemoryWatchdog {
 public:
  enum PressureLevel : uint8_t {
    kOutOfMemory = 0,
    kCritical,
    kWarning,
    kNormal,
    kNumLevels,
  };

  void Init(Executor* executor);

  fbl::RefPtr<EventDispatcher> GetMemPressureEvent(uint32_t kind);

 private:
  // The callback provided to |pmm_init_reclamation|.
  static void AvailableStateUpdatedCallback(void* context, uint8_t idx);
  void AvailableStateUpdate(uint8_t idx);
  // The callback provided to the |eviction_trigger_| timer.
  static void EvictionTriggerCallback(Timer* timer, zx_time_t now, void* arg);
  void EvictionTrigger();

  void WorkerThread() __NO_RETURN;

  // Helper called by the WorkerThread when OOM conditions are hit.
  void OnOom();

  // Kernel-owned events used to signal userspace at different levels of memory pressure.
  ktl::array<fbl::RefPtr<EventDispatcher>, PressureLevel::kNumLevels> mem_pressure_events_;

  // Event used for communicating memory state between the mem_avail_state_updated_cb callback and
  // the WorkerThread.
  AutounsignalEvent mem_state_signal_;

  ktl::atomic<PressureLevel> mem_event_idx_ = PressureLevel::kNormal;
  PressureLevel prev_mem_event_idx_ = mem_event_idx_;

  // Used to delay signaling memory level transitions in the case of rapid changes.
  static constexpr zx_time_t kHysteresisSeconds_ = ZX_SEC(10);

  // Tracks last time the memory state was evaluated (and signaled if required).
  zx_time_t prev_mem_state_eval_time_ = ZX_TIME_INFINITE_PAST;

  // The highest pressure level we trigger eviction at, OOM being the lowest pressure level (0).
  PressureLevel max_eviction_level_ = PressureLevel::kCritical;

  // The free memory target to aim for when we trigger eviction.
  uint64_t free_mem_target_ = 0;

  // Current minimum amount of memory we want the triggered eviction to reclaim.
  uint64_t min_free_target_ = 0;

  // A timer is used to trigger eviction so that user space is given a chance to act upon a memory
  // pressure signal first.
  Timer eviction_trigger_;

  Executor* executor_;
};

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_MEMORY_WATCHDOG_H_
