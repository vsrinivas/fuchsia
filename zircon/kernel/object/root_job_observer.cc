// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cmdline.h>
#include <lib/debuglog.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/compiler.h>

#include <object/root_job_observer.h>
#include <platform/halt_helper.h>

namespace {

bool HasChild(zx_signals_t state) {
  bool no_child = (state & ZX_JOB_NO_JOBS) && (state & ZX_JOB_NO_PROCESSES);
  return !no_child;
}

__NO_RETURN void Halt() {
  const char* notice = gCmdline.GetString("kernel.root-job.notice");
  if (notice != nullptr) {
    printf("root-job: notice: %s\n", notice);
  }

  const char* behavior = gCmdline.GetString("kernel.root-job.behavior");
  if (behavior == nullptr) {
    behavior = "reboot";
  }

  printf("root-job: taking %s action\n", behavior);
  const zx_time_t dlog_deadline = current_time() + ZX_SEC(5);
  dlog_shutdown(dlog_deadline);

  // About to call |platform_halt|, which never returns.
  if (!strcmp(behavior, "halt")) {
    platform_halt(HALT_ACTION_HALT, ZirconCrashReason::NoCrash);
  } else if (!strcmp(behavior, "bootloader")) {
    platform_halt(HALT_ACTION_REBOOT_BOOTLOADER, ZirconCrashReason::NoCrash);
  } else if (!strcmp(behavior, "recovery")) {
    platform_halt(HALT_ACTION_REBOOT_RECOVERY, ZirconCrashReason::NoCrash);
  } else if (!strcmp(behavior, "shutdown")) {
    platform_halt(HALT_ACTION_SHUTDOWN, ZirconCrashReason::NoCrash);
  } else {
    platform_halt(HALT_ACTION_REBOOT, ZirconCrashReason::NoCrash);
  }
}

}  // anonymous namespace

RootJobObserver::RootJobObserver(fbl::RefPtr<JobDispatcher> root_job)
    : RootJobObserver(ktl::move(root_job), Halt) {}

RootJobObserver::RootJobObserver(fbl::RefPtr<JobDispatcher> root_job, fbl::Closure callback)
    : root_job_(ktl::move(root_job)), callback_(ktl::move(callback)) {
  root_job_->AddObserver(this);
}

RootJobObserver::~RootJobObserver() { root_job_->RemoveObserver(this); }

StateObserver::Flags RootJobObserver::OnInitialize(zx_signals_t initial_state) {
  return 0;
}

StateObserver::Flags RootJobObserver::OnStateChange(zx_signals_t new_state) {
  // Remember, the |root_job_|'s Dispatcher lock is held for the duration of
  // this method.  Take care to avoid calling anything that might attempt to
  // acquire that lock.

  // If we don't have any children, trigger the callback.
  //
  // If the root job is itself killed, all children processes and jobs will
  // first be removed, also causing the "HasChild" check to fail.
  if (!HasChild(new_state)) {
    callback_();
    return kNeedRemoval;
  }

  return 0;
}

StateObserver::Flags RootJobObserver::OnCancel(const Handle* handle) { return 0; }
