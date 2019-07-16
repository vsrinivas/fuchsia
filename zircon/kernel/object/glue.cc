// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file defines:
// * Initialization code for kernel/object module
// * Singleton instances and global locks
// * Helper functions

#include <inttypes.h>
#include <kernel/cmdline.h>
#include <lk/init.h>
#include <object/diagnostics.h>
#include <object/event_dispatcher.h>
#include <object/excp_port.h>
#include <object/job_dispatcher.h>
#include <object/port_dispatcher.h>
#include <platform/halt_helper.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

// All jobs and processes are rooted at the |root_job|.
static fbl::RefPtr<JobDispatcher> root_job;

fbl::RefPtr<JobDispatcher> GetRootJobDispatcher() { return root_job; }

// Kernel-owned event that is used to signal userspace before taking action in OOM situation.
static fbl::RefPtr<EventDispatcher> low_mem_event;
// Event used for communicating lowmem state between the lowmem callback and the oom thread.
static Event low_mem_signal;

fbl::RefPtr<EventDispatcher> GetLowMemEvent() { return low_mem_event; }

// Callback used with |pmm_init_reclamation|.
static void mem_avail_state_updated_cb(uint8_t idx) {
  zx_status_t status;
  printf("OOM: memory availability state %u\n", idx);

  if (idx == 0) {
    status = low_mem_event->user_signal_self(0, ZX_EVENT_SIGNALED);
    low_mem_signal.Signal();
  } else {
    status = low_mem_event->user_signal_self(ZX_EVENT_SIGNALED, 0);
    low_mem_signal.Unsignal();
  }
  if (status != ZX_OK) {
    printf("OOM: signal low mem failed: %d\n", status);
  }
}

static int oom_thread(void* unused) {
  while (true) {
    low_mem_signal.Wait(Deadline::infinite());

#if defined(ENABLE_KERNEL_DEBUGGING_FEATURES)
    // See ZX-3637 for the product details on when this path vs. the reboot
    // should be used.

    bool found = false;
    JobDispatcher::ForEachJob([&found](JobDispatcher* job) {
      if (job->get_kill_on_oom()) {
        // The traversal order of ForEachJob() is going to favor killing newer
        // jobs, this helps in case more than one is eligible.
        if (job->Kill(ZX_TASK_RETCODE_OOM_KILL)) {
          found = true;
          char name[ZX_MAX_NAME_LEN];
          job->get_name(name);
          printf("OOM: killing job %6" PRIu64 " '%s'\n", job->get_koid(), name);
          return ZX_ERR_STOP;
        }
      }
      return ZX_OK;
    });

    if (!found) {
      printf("OOM: no alive job has a kill bit\n");
    }

    // Since killing is asynchronous, sleep for a short period for the system to quiesce. This
    // prevents us from rapidly killing more jobs than necessary. And if we don't find a
    // killable job, don't just spin since the next iteration probably won't find a one either.
    thread_sleep_relative(ZX_MSEC(500));
#else
    const int kSleepSeconds = 8;
    printf("OOM: pausing for %ds after low mem signal\n", kSleepSeconds);
    zx_status_t status = thread_sleep_relative(ZX_SEC(kSleepSeconds));
    if (status != ZX_OK) {
      printf("OOM: sleep failed: %d\n", status);
    }
    printf("OOM: rebooting\n");
    platform_graceful_halt_helper(HALT_ACTION_REBOOT);
#endif
  }
}

static void object_glue_init(uint level) TA_NO_THREAD_SAFETY_ANALYSIS {
  Handle::Init();
  root_job = JobDispatcher::CreateRootJob();
  PortDispatcher::Init();

  KernelHandle<EventDispatcher> event;
  zx_rights_t rights;
  zx_status_t status = EventDispatcher::Create(0, &event, &rights);
  if (status != ZX_OK) {
    panic("low mem event create: %d\n", status);
  }
  low_mem_event = event.release();

  if (cmdline_get_bool("kernel.oom.enable", true)) {
    auto redline = cmdline_get_uint64("kernel.oom.redline-mb", 50) * MB;
    zx_status_t status = pmm_init_reclamation(&redline, 1, MB, mem_avail_state_updated_cb);
    if (status != ZX_OK) {
      panic("failed to initialize pmm reclamation: %d\n", status);
    }

    auto thread = thread_create("oom-thread", oom_thread, nullptr, HIGH_PRIORITY);
    DEBUG_ASSERT(thread);
    thread_detach(thread);
    thread_resume(thread);
  }
}

LK_INIT_HOOK(libobject, object_glue_init, LK_INIT_LEVEL_THREADING)
