// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v1/suspend_resume_manager.h"

#include <errno.h>
#include <zircon/errors.h>

#include "src/devices/lib/log/log.h"

SuspendResumeManager::SuspendResumeManager(Coordinator* coordinator, zx::duration suspend_timeout)
    : coordinator_(coordinator), suspend_handler_(coordinator, suspend_timeout) {}

bool SuspendResumeManager::InSuspend() const { return suspend_handler_.InSuspend(); }

bool SuspendResumeManager::InResume() const {
  return (resume_context_.flags() == ResumeContext::Flags::kResume);
}

void SuspendResumeManager::Suspend(uint32_t flags, SuspendCallback callback) {
  if (InResume()) {
    LOGF(ERROR, "Aborting system-suspend, a system resume is in progress");
    if (callback) {
      callback(ZX_ERR_UNAVAILABLE);
    }
    return;
  }

  suspend_handler_.Suspend(flags, std::move(callback));
}

void SuspendResumeManager::Resume(SystemPowerState target_state, ResumeCallback callback) {
  if (!coordinator_->sys_device()->proxy()) {
    return;
  }

  if (InSuspend()) {
    return;
  }

  auto schedule_resume = [this, callback](fbl::RefPtr<Device> dev) {
    auto completion = [this, dev, callback](zx_status_t status) {
      dev->clear_active_resume();

      auto& ctx = resume_context_;
      if (status != ZX_OK) {
        LOGF(ERROR, "Failed to resume: %s", zx_status_get_string(status));
        ctx.set_flags(ResumeContext::Flags::kSuspended);
        auto task = ctx.take_pending_task(*dev);
        callback(status);
        return;
      }

      std::optional<fbl::RefPtr<ResumeTask>> task = ctx.take_pending_task(*dev);
      if (task.has_value()) {
        ctx.push_completed_task(std::move(task.value()));
      } else {
        // Something went wrong
        LOGF(ERROR, "Failed to resume, cannot find matching pending task");
        callback(ZX_ERR_INTERNAL);
        return;
      }

      if (ctx.pending_tasks_is_empty()) {
        async::PostTask(coordinator_->dispatcher(), [this, callback] {
          resume_context_.reset_completed_tasks();
          callback(ZX_OK);
        });
      }
    };

    auto task = ResumeTask::Create(dev, static_cast<uint32_t>(resume_context_.target_state()),
                                   std::move(completion));
    resume_context_.push_pending_task(task);
    dev->SetActiveResume(std::move(task));
  };

  resume_context_ = ResumeContext(ResumeContext::Flags::kResume, target_state), std::move(callback);
  for (auto& dev : coordinator_->devices()) {
    schedule_resume(fbl::RefPtr(&dev));
    if (dev.proxy()) {
      schedule_resume(dev.proxy());
    }
  }
  schedule_resume(coordinator_->sys_device());
  schedule_resume(coordinator_->sys_device()->proxy());

  // Post a delayed task in case drivers do not complete the resume.
  auto status = async::PostDelayedTask(
      coordinator_->dispatcher(),
      [this, callback] {
        if (!InResume()) {
          return;
        }
        LOGF(ERROR, "System resume timed out");
        callback(ZX_ERR_TIMED_OUT);
        // TODO(ravoorir): Figure out what is the best strategy
        // of for recovery here. Should we put back all devices
        // in suspend? In future, this could be more interactive
        // with the UI.
      },
      coordinator_->resume_timeout());
  if (status != ZX_OK) {
    LOGF(ERROR, "Failure to create resume timeout watchdog");
  }
}

// TODO(fxbug.dev/42257): Temporary helper to convert state to flags.
// Will be removed eventually.
uint32_t SuspendResumeManager::GetSuspendFlagsFromSystemPowerState(
    statecontrol_fidl::wire::SystemPowerState state) const {
  switch (state) {
    case statecontrol_fidl::wire::SystemPowerState::kFullyOn:
      return 0;
    case statecontrol_fidl::wire::SystemPowerState::kReboot:
      return DEVICE_SUSPEND_FLAG_REBOOT;
    case statecontrol_fidl::wire::SystemPowerState::kRebootBootloader:
      return DEVICE_SUSPEND_FLAG_REBOOT_BOOTLOADER;
    case statecontrol_fidl::wire::SystemPowerState::kRebootRecovery:
      return DEVICE_SUSPEND_FLAG_REBOOT_RECOVERY;
    case statecontrol_fidl::wire::SystemPowerState::kPoweroff:
      return DEVICE_SUSPEND_FLAG_POWEROFF;
    case statecontrol_fidl::wire::SystemPowerState::kMexec:
      return DEVICE_SUSPEND_FLAG_MEXEC;
    case statecontrol_fidl::wire::SystemPowerState::kSuspendRam:
      return DEVICE_SUSPEND_FLAG_SUSPEND_RAM;
    default:
      return 0;
  }
}
