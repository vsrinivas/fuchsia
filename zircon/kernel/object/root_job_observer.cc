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

__NO_RETURN void Halt() {
  const char* notice = gCmdline.GetString(kernel_option::kRootJobNotice);
  if (notice != nullptr) {
    printf("root-job: notice: %s\n", notice);
  }

  const char* behavior = gCmdline.GetString(kernel_option::kRootJobBehavior);
  if (behavior == nullptr) {
    behavior = "reboot";
  }

  printf("root-job: taking %s action\n", behavior);
  const zx_time_t dlog_deadline = current_time() + ZX_SEC(5);
  dlog_shutdown(dlog_deadline);

  // About to call |platform_halt|, which never returns.
  if (!strcmp(behavior, "halt")) {
    platform_halt(HALT_ACTION_HALT, ZirconCrashReason::UserspaceRootJobTermination);
  } else if (!strcmp(behavior, "bootloader")) {
    platform_halt(HALT_ACTION_REBOOT_BOOTLOADER, ZirconCrashReason::UserspaceRootJobTermination);
  } else if (!strcmp(behavior, "recovery")) {
    platform_halt(HALT_ACTION_REBOOT_RECOVERY, ZirconCrashReason::UserspaceRootJobTermination);
  } else if (!strcmp(behavior, "shutdown")) {
    platform_halt(HALT_ACTION_SHUTDOWN, ZirconCrashReason::UserspaceRootJobTermination);
  } else {
    platform_halt(HALT_ACTION_REBOOT, ZirconCrashReason::UserspaceRootJobTermination);
  }
}

}  // anonymous namespace

RootJobObserver::RootJobObserver(fbl::RefPtr<JobDispatcher> root_job, Handle* root_job_handle_)
    : RootJobObserver(ktl::move(root_job), root_job_handle_, Halt) {}

RootJobObserver::RootJobObserver(fbl::RefPtr<JobDispatcher> root_job, Handle* root_job_handle,
                                 fbl::Closure callback)
    : root_job_(ktl::move(root_job)), callback_(std::move(callback)) {
  root_job_->AddObserver(this, root_job_handle, ZX_JOB_NO_CHILDREN);
}

RootJobObserver::~RootJobObserver() { root_job_->RemoveObserver(this); }

void RootJobObserver::OnMatch(zx_signals_t signals) {
  // Remember, the |root_job_|'s Dispatcher lock is held for the duration of
  // this method.  Take care to avoid calling anything that might attempt to
  // acquire that lock.
  callback_();
}

void RootJobObserver::OnCancel(zx_signals_t signals) {}
