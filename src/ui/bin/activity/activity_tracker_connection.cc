// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/activity_tracker_connection.h"

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <src/lib/fxl/logging.h>

namespace activity {

void ActivityTrackerConnection::Stop() {
  for (const auto& id : ongoing_activities_) {
    zx_status_t status = state_machine_driver_->EndOngoingActivity(id, async::Now(dispatcher_));
    // Assert here since failing silently may result in a leaked activity which would stall the
    // state machine from timing out.
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to clean up activity %d: %s", id,
                  zx_status_get_string(status));
  }
  ongoing_activities_.clear();
}

void ActivityTrackerConnection::ReportDiscreteActivity(
    fuchsia::ui::activity::DiscreteActivity activity, zx_time_t time) {
  zx_status_t status = state_machine_driver_->ReceiveDiscreteActivity(activity, zx::time(time));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "activity-service: Failed to receive activity: "
                   << zx_status_get_string(status);
    binding_.Close(status);
  }
}

void ActivityTrackerConnection::StartOngoingActivity(
    fuchsia::ui::activity::OngoingActivity activity, zx_time_t time,
    StartOngoingActivityCallback callback) {
  auto id = GenerateActivityId();
  zx_status_t status = state_machine_driver_->StartOngoingActivity(id, zx::time(time));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "activity-service: Failed to start activity: "
                   << zx_status_get_string(status);
    binding_.Close(status);
    return;
  }
  ongoing_activities_.insert(id);
  callback(id);
}

void ActivityTrackerConnection::EndOngoingActivity(OngoingActivityId id, zx_time_t time) {
  auto iter = ongoing_activities_.find(id);
  if (iter == ongoing_activities_.end()) {
    FXL_LOG(ERROR) << "activity-service: Invalid activity ID: " << id;
    binding_.Close(ZX_ERR_NOT_FOUND);
    return;
  }
  zx_status_t status = state_machine_driver_->EndOngoingActivity(id, zx::time(time));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "activity-service: Failed to end activity: " << zx_status_get_string(status);
    binding_.Close(status);
    return;
  }
  ongoing_activities_.erase(iter);
}

}  // namespace activity
