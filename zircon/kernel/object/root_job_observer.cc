// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/boot-options/boot-options.h>
#include <lib/boot-options/types.h>
#include <lib/debuglog.h>
#include <platform.h>
#include <string.h>
#include <zircon/boot/crash-reason.h>
#include <zircon/compiler.h>

#include <object/root_job_observer.h>
#include <platform/halt_helper.h>

namespace {

// May or may not return.
void Halt() {
  const char* notice = gBootOptions->root_job_notice.data();
  if (!TakeHaltToken()) {
    printf("root-job: halt/reboot already in progress; returning\n");
    return;
  }
  // We now have the halt token so we're committed.  There is no return from this point.

  if (strlen(notice) != 0) {
    printf("root-job: notice: %s\n", notice);
  }

  ktl::string_view action_name;
  platform_halt_action action;
  switch (gBootOptions->root_job_behavior) {
    case RootJobBehavior::kHalt:
      action = HALT_ACTION_HALT;
      action_name = kRootJobBehaviorHaltName;
      break;
    case RootJobBehavior::kBootloader:
      action = HALT_ACTION_REBOOT_BOOTLOADER;
      action_name = kRootJobBehaviorBootloaderName;
      break;
    case RootJobBehavior::kRecovery:
      action = HALT_ACTION_REBOOT_RECOVERY;
      action_name = kRootJobBehaviorRecoveryName;
      break;
    case RootJobBehavior::kShutdown:
      action = HALT_ACTION_SHUTDOWN;
      action_name = kRootJobBehaviorShutdownName;
      break;
    case RootJobBehavior::kReboot:
    default:
      action = HALT_ACTION_REBOOT;
      action_name = kRootJobBehaviorRebootName;
      break;
  }

  printf("root-job: taking %s action\n", action_name.data());
  const zx_time_t dlog_deadline = current_time() + ZX_SEC(5);
  dlog_shutdown(dlog_deadline);
  // Does not return.
  platform_halt(action, ZirconCrashReason::UserspaceRootJobTermination);
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
