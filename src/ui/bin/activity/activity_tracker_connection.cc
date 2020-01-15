// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/activity_tracker_connection.h"

#include <fuchsia/ui/activity/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/fxl/logging.h"

namespace activity {

ActivityTrackerConnection::ActivityTrackerConnection(
    StateMachineDriver* state_machine_driver, async_dispatcher_t* dispatcher,
    fidl::InterfaceRequest<fuchsia::ui::activity::Tracker> request)
    : state_machine_driver_(state_machine_driver),
      dispatcher_(dispatcher),
      binding_(this, std::move(request), dispatcher) {}

ActivityTrackerConnection::~ActivityTrackerConnection() { Stop(); }

void ActivityTrackerConnection::Stop() {
  for (const auto& id : ongoing_activities_) {
    zx_status_t status =
        state_machine_driver_->EndOngoingActivity(id, async::Now(dispatcher_), []() {});
    // Assert here since failing silently may result in a leaked activity which would stall the
    // state machine from timing out.
    ZX_ASSERT_MSG(status == ZX_OK, "Failed to clean up activity %d: %s", id,
                  zx_status_get_string(status));
  }
  ongoing_activities_.clear();
}

void ActivityTrackerConnection::ReportDiscreteActivity(
    fuchsia::ui::activity::DiscreteActivity activity, zx_time_t time,
    ReportDiscreteActivityCallback callback) {
  if (time < last_activity_time_.get()) {
    FXL_LOG(ERROR) << "activity-service: Received out-of-order events from client.";
    binding_.Close(ZX_ERR_OUT_OF_RANGE);
    return;
  }
  last_activity_time_ = zx::time(time);
  zx_status_t status =
      state_machine_driver_->ReceiveDiscreteActivity(activity, zx::time(time), std::move(callback));
  if (status != ZX_OK) {
    if (status == ZX_ERR_OUT_OF_RANGE) {
      FXL_LOG(WARNING) << "activity-service: Ignoring activity due to stale timestamp (" << time
                       << ")";
    } else {
      FXL_LOG(ERROR) << "activity-service: Failed to receive activity: "
                     << zx_status_get_string(status);
      binding_.Close(status);
    }
  }
}

void ActivityTrackerConnection::StartOngoingActivity(
    OngoingActivityId id, fuchsia::ui::activity::OngoingActivity activity, zx_time_t time,
    StartOngoingActivityCallback callback) {
  if (ongoing_activities_.find(id) != ongoing_activities_.end()) {
    FXL_LOG(ERROR) << "activity-service: activity already started: " << id;
    binding_.Close(ZX_ERR_ALREADY_EXISTS);
    return;
  }
  if (time < last_activity_time_.get()) {
    FXL_LOG(ERROR) << "activity-service: Received out-of-order events from client.";
    binding_.Close(ZX_ERR_OUT_OF_RANGE);
    return;
  }
  last_activity_time_ = zx::time(time);
  zx_status_t status =
      state_machine_driver_->StartOngoingActivity(id, zx::time(time), std::move(callback));
  if (status == ZX_OK) {
    ongoing_activities_.insert(id);
  } else {
    if (status == ZX_ERR_OUT_OF_RANGE) {
      FXL_LOG(WARNING) << "activity-service: Ignoring activity due to stale timestamp (" << time
                       << ")";
      // Ignore the activity (don't close the binding or send an error to the client)
    } else {
      FXL_LOG(ERROR) << "activity-service: Failed to start activity: "
                     << zx_status_get_string(status);
      binding_.Close(status);
    }
  }
}

void ActivityTrackerConnection::EndOngoingActivity(OngoingActivityId id, zx_time_t time,
                                                   EndOngoingActivityCallback callback) {
  if (time < last_activity_time_.get()) {
    FXL_LOG(ERROR) << "activity-service: Received out-of-order events from client.";
    binding_.Close(ZX_ERR_OUT_OF_RANGE);
    return;
  }
  last_activity_time_ = zx::time(time);
  auto iter = ongoing_activities_.find(id);
  if (iter == ongoing_activities_.end()) {
    FXL_LOG(ERROR) << "activity-service: Invalid activity ID: " << id;
    binding_.Close(ZX_ERR_NOT_FOUND);
    return;
  }
  zx_status_t status =
      state_machine_driver_->EndOngoingActivity(id, zx::time(time), std::move(callback));
  if (status == ZX_OK) {
    ongoing_activities_.erase(iter);
  } else {
    if (status == ZX_ERR_OUT_OF_RANGE) {
      FXL_LOG(WARNING) << "activity-service: Ignoring activity due to stale timestamp (" << time
                       << ")";
      // Ignore the activity (don't close the binding or send an error to the client)
    } else {
      FXL_LOG(ERROR) << "activity-service: Failed to end activity: "
                     << zx_status_get_string(status);
      binding_.Close(status);
    }
  }
}

}  // namespace activity
