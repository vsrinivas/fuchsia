// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V1_RESUME_CONTEXT_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V1_RESUME_CONTEXT_H_

#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>

#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"

using statecontrol_fidl::wire::SystemPowerState;

// Tracks the global resume state that is currently in progress.
class ResumeContext {
 public:
  enum class Flags : uint32_t {
    kResume = 0u,
    kSuspended = 1u,
  };
  ResumeContext() = default;

  ResumeContext(Flags flags, SystemPowerState resume_state)
      : target_state_(resume_state), flags_(flags) {}

  ~ResumeContext() {}

  ResumeContext(ResumeContext&&) = default;
  ResumeContext& operator=(ResumeContext&&) = default;

  Flags flags() const { return flags_; }
  void set_flags(Flags flags) { flags_ = flags; }

  void push_pending_task(fbl::RefPtr<ResumeTask> task) {
    pending_resume_tasks_.push_back(std::move(task));
  }
  void push_completed_task(fbl::RefPtr<ResumeTask> task) {
    completed_resume_tasks_.push_back(std::move(task));
  }

  bool pending_tasks_is_empty() const { return pending_resume_tasks_.is_empty(); }
  bool completed_tasks_is_empty() const { return completed_resume_tasks_.is_empty(); }

  std::optional<fbl::RefPtr<ResumeTask>> take_pending_task(Device& dev) {
    for (size_t i = 0; i < pending_resume_tasks_.size(); i++) {
      if (&pending_resume_tasks_[i]->device() == &dev) {
        auto task = pending_resume_tasks_.erase(i);
        return std::move(task);
      }
    }
    return {};
  }

  void reset_completed_tasks() { completed_resume_tasks_.reset(); }

  SystemPowerState target_state() const { return target_state_; }

 private:
  fbl::Vector<fbl::RefPtr<ResumeTask>> pending_resume_tasks_;
  fbl::Vector<fbl::RefPtr<ResumeTask>> completed_resume_tasks_;
  SystemPowerState target_state_;
  Flags flags_ = Flags::kSuspended;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V1_RESUME_CONTEXT_H_
