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
#include <object/excp_port.h>
#include <object/job_dispatcher.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>

#include <platform/halt_helper.h>

#include <zircon/syscalls/object.h>
#include <zircon/types.h>

// All jobs and processes are rooted at the |root_job|.
static fbl::RefPtr<JobDispatcher> root_job;

fbl::RefPtr<JobDispatcher> GetRootJobDispatcher() {
    return root_job;
}

static void oom_lowmem(size_t shortfall_bytes) {
    printf("OOM: oom_lowmem(shortfall_bytes=%zu) called\n", shortfall_bytes);

#if defined(ENABLE_KERNEL_DEBUGGING_FEATURES)
    // See ZX-3637 for why this is currently disabled outside of the bringup
    // product configuration.

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
    printf("OOM: rebooting\n");
    platform_graceful_halt_helper(HALT_ACTION_REBOOT);
#endif
}

static void object_glue_init(uint level) TA_NO_THREAD_SAFETY_ANALYSIS {
    Handle::Init();
    root_job = JobDispatcher::CreateRootJob();
    PortDispatcher::Init();
    // Be sure to update kernel_cmdline.md if any of these defaults change.
    oom_init(cmdline_get_bool("kernel.oom.enable", true),
             ZX_SEC(cmdline_get_uint64("kernel.oom.sleep-sec", 1)),
             cmdline_get_uint64("kernel.oom.redline-mb", 50) * MB,
             oom_lowmem);
}

LK_INIT_HOOK(libobject, object_glue_init, LK_INIT_LEVEL_THREADING)
