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

#include <lib/oom.h>

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

fbl::RefPtr<JobDispatcher> GetRootJobDispatcher() {
    return root_job;
}

// Kernel-owned event that is signaled before taking action in OOM situation.
static fbl::RefPtr<EventDispatcher> low_mem_event;

fbl::RefPtr<EventDispatcher> GetLowMemEvent() {
    return low_mem_event;
}

static void oom_lowmem(size_t shortfall_bytes) {
    zx_status_t status;
    printf("OOM: oom_lowmem(shortfall_bytes=%zu) called\n", shortfall_bytes);

    status = low_mem_event->user_signal_self(0, ZX_EVENT_SIGNALED);
    if (status != ZX_OK) {
        printf("OOM: signal low mem failed: %d\n", status);
    }

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
#else
    const int kSleepSeconds = 8;
    printf("OOM: pausing for %ds after low mem signal\n", kSleepSeconds);
    status = thread_sleep_relative(ZX_SEC(kSleepSeconds));
    if (status != ZX_OK) {
        printf("OOM: sleep failed: %d\n", status);
    }
    printf("OOM: rebooting\n");
    platform_graceful_halt_helper(HALT_ACTION_REBOOT);
#endif
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

    // Be sure to update kernel_cmdline.md if any of these defaults change.
    oom_init(cmdline_get_bool("kernel.oom.enable", true),
             ZX_SEC(cmdline_get_uint64("kernel.oom.sleep-sec", 1)),
             cmdline_get_uint64("kernel.oom.redline-mb", 50) * MB,
             oom_lowmem);
}

LK_INIT_HOOK(libobject, object_glue_init, LK_INIT_LEVEL_THREADING)
