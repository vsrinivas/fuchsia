// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/pressure.h"

#include <fuchsia/boot/c/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <sys/stat.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include "src/lib/fxl/logging.h"

namespace monitor {

Pressure::Pressure(bool watch_for_changes) {
  if (InitMemPressureEvents() != ZX_OK) {
    return;
  }

  if (watch_for_changes) {
    loop_.StartThread("memory-pressure-loop");
    task_.Post(loop_.dispatcher());
  }
}

zx_status_t Pressure::InitMemPressureEvents() {
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx::channel::create returned " << zx_status_get_string(status);
    return status;
  }
  const char* root_job_svc = "/svc/fuchsia.boot.RootJobForInspect";
  status = fdio_service_connect(root_job_svc, remote.release());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "fdio_service_connect returned " << zx_status_get_string(status);
    return status;
  }

  zx::job root_job;
  status = fuchsia_boot_RootJobForInspectGet(local.get(), root_job.reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "fuchsia_boot_RootJobForInspectGet returned " << zx_status_get_string(status);
    return status;
  }

  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_MEMORY_PRESSURE_CRITICAL,
                               events_[Level::kCritical].reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_system_get_event [CRITICAL] returned " << zx_status_get_string(status);
    return status;
  }

  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_MEMORY_PRESSURE_WARNING,
                               events_[Level::kWarning].reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_system_get_event [WARNING] returned " << zx_status_get_string(status);
    return status;
  }

  status = zx_system_get_event(root_job.get(), ZX_SYSTEM_EVENT_MEMORY_PRESSURE_NORMAL,
                               events_[Level::kNormal].reset_and_get_address());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_system_get_event [NORMAL] returned " << zx_status_get_string(status);
    return status;
  }

  for (size_t i = 0; i < Level::kNumLevels; i++) {
    wait_items_[i].handle = events_[i].get();
    wait_items_[i].waitfor = ZX_EVENT_SIGNALED;
    wait_items_[i].pending = 0;
  }

  return ZX_OK;
}

void Pressure::WatchForChanges() {
  WaitOnLevelChange();

  task_.Post(loop_.dispatcher());
}

void Pressure::WaitOnLevelChange() {
  // Wait on all events the first time around.
  size_t num_wait_items = (level_ == Level::kNumLevels) ? Level::kNumLevels : Level::kNumLevels - 1;

  zx_status_t status = zx_object_wait_many(wait_items_.data(), num_wait_items, ZX_TIME_INFINITE);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "zx_object_wait_many returned " << zx_status_get_string(status);
    return;
  }

  for (size_t i = 0; i < Level::kNumLevels; i++) {
    if (wait_items_[i].pending) {
      wait_items_[i].pending = 0;
      OnLevelChanged(wait_items_[i].handle);

      // Move the event currently asserted to the end of the array.
      // Wait on only the first |kNumLevels| - 1 items next time around.
      std::swap(wait_items_[i].handle, wait_items_[Level::kNumLevels - 1].handle);
      break;
    }
  }
}

void Pressure::OnLevelChanged(zx_handle_t handle) {
  Level old_level = level_;
  for (size_t i = 0; i < Level::kNumLevels; i++) {
    if (events_[i].get() == handle) {
      level_ = Level(i);
      break;
    }
  }

  FXL_LOG(INFO) << "Memory pressure level changed from " << kLevelNames[old_level] << " to "
                << kLevelNames[level_];
}

}  // namespace monitor
