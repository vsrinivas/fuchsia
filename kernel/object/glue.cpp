// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// This file defines:
// * Initialization code for kernel/object module
// * Singleton instances and global locks
// * Helper functions
//
// TODO(dbort): Split this file into self-consistent pieces.

#include <inttypes.h>

#include <trace.h>

#include <kernel/cmdline.h>

#include <lk/init.h>

#include <lib/oom.h>

#include <object/diagnostics.h>
#include <object/excp_port.h>
#include <object/job_dispatcher.h>
#include <object/policy_manager.h>
#include <object/port_dispatcher.h>
#include <object/process_dispatcher.h>

#include <fbl/function.h>

#include <zircon/types.h>

#define LOCAL_TRACE 0

// All jobs and processes are rooted at the |root_job|.
static fbl::RefPtr<JobDispatcher> root_job;

// The singleton policy manager, for jobs and processes. This is
// not a Dispatcher, just a plain class.
static PolicyManager* policy_manager;

fbl::RefPtr<JobDispatcher> GetRootJobDispatcher() {
    return root_job;
}

PolicyManager* GetSystemPolicyManager() {
    return policy_manager;
}

// Counts and optionally prints all job/process descendants of a job.
namespace {
class OomJobEnumerator final : public JobEnumerator {
public:
    // If |prefix| is non-null, also prints each job/process.
    OomJobEnumerator(const char* prefix)
        : prefix_(prefix) { reset_counts(); }
    void reset_counts() {
        num_jobs_ = 0;
        num_processes_ = 0;
        num_running_processes_ = 0;
    }
    size_t num_jobs() const { return num_jobs_; }
    size_t num_processes() const { return num_processes_; }
    size_t num_running_processes() const { return num_running_processes_; }

private:
    bool OnJob(JobDispatcher* job) final {
        if (prefix_ != nullptr) {
            char name[ZX_MAX_NAME_LEN];
            job->get_name(name);
            printf("%sjob %6" PRIu64 " '%s'\n", prefix_, job->get_koid(), name);
        }
        num_jobs_++;
        return true;
    }
    bool OnProcess(ProcessDispatcher* process) final {
        // Since we want to free memory by actually killing something, only
        // count running processes that aren't attached to a debugger.
        // It's a race, but will stop us from re-killing a job that only has
        // blocked-by-debugger processes.
        zx_info_process_t info = {};
        process->GetInfo(&info);
        if (info.started && !info.exited && !info.debugger_attached) {
            num_running_processes_++;
        }
        if (prefix_ != nullptr) {
            const char* tag = "new";
            if (info.started) {
                tag = "run";
            }
            if (info.exited) {
                tag = "dead";
            }
            if (info.debugger_attached) {
                tag = "dbg";
            }
            char name[ZX_MAX_NAME_LEN];
            process->get_name(name);
            printf("%sproc %5" PRIu64 " %4s '%s'\n",
                   prefix_, process->get_koid(), tag, name);
        }
        num_processes_++;
        return true;
    }

    const char* prefix_;
    size_t num_jobs_;
    size_t num_processes_;
    size_t num_running_processes_;
};
} // namespace

// Called from a dedicated kernel thread when the system is low on memory.
static void oom_lowmem(size_t shortfall_bytes) {
    printf("OOM: oom_lowmem(shortfall_bytes=%zu) called\n", shortfall_bytes);
    printf("OOM: Process mapped committed bytes:\n");
    DumpProcessMemoryUsage("OOM:   ", /*min_pages=*/8 * MB / PAGE_SIZE);
    printf("OOM: Finding a job to kill...\n");

    OomJobEnumerator job_counter(nullptr);
    OomJobEnumerator job_printer("OOM:        + ");

    bool killed = false;
    int next = 3; // Used to print a few "up next" jobs.
    JobDispatcher::ForEachJobByImportance([&](JobDispatcher* job) {
        // TODO(dbort): Consider adding an "immortal" bit on jobs and skip them
        // here if they (and and all of their ancestors) have it set.
        bool kill = false;
        if (!killed) {
            // We want to kill a job that will actually free memory by dying, so
            // look for one with running process descendants (i.e., started,
            // non-exited, not blocked in a debugger).
            job_counter.reset_counts();
            job->EnumerateChildren(&job_counter, /*recurse=*/true);
            kill = job_counter.num_running_processes() > 0;
        }

        const char* tag;
        if (kill) {
            tag = "*KILL*";
        } else if (!killed) {
            tag = "(skip)";
        } else {
            tag = "(next)";
        }
        char name[ZX_MAX_NAME_LEN];
        job->get_name(name);
        printf("OOM:   %s job %6" PRIu64 " '%s'\n", tag, job->get_koid(), name);
        if (kill) {
            // Print the descendants of the job we're about to kill.
            job_printer.reset_counts();
            job->EnumerateChildren(&job_printer, /*recurse=*/true);
            printf("OOM:        = %zu running procs (%zu total), %zu jobs\n",
                   job_printer.num_running_processes(),
                   job_printer.num_processes(), job_printer.num_jobs());
            // TODO(dbort): Join on dying processes/jobs to make sure we've
            // freed memory before returning control to the OOM thread?
            // TODO(ZX-961): 'kill -9' these processes (which will require new
            // ProcessDispatcher features) so we can reclaim the memory of
            // processes that are stuck in a debugger or in the crashlogger.
            job->Kill();
            killed = true;
        } else if (killed) {
            if (--next == 0) {
                return ZX_ERR_STOP;
            }
        }
        return ZX_OK;
    });
}

static void object_glue_init(uint level) TA_NO_THREAD_SAFETY_ANALYSIS {
    Handle::Init();
    root_job = JobDispatcher::CreateRootJob();
    policy_manager = PolicyManager::Create();
    PortDispatcher::Init();
    // Be sure to update kernel_cmdline.md if any of these defaults change.
    oom_init(cmdline_get_bool("kernel.oom.enable", true),
             ZX_SEC(cmdline_get_uint64("kernel.oom.sleep-sec", 1)),
             cmdline_get_uint64("kernel.oom.redline-mb", 50) * MB,
             oom_lowmem);
}

LK_INIT_HOOK(libobject, object_glue_init, LK_INIT_LEVEL_THREADING);
