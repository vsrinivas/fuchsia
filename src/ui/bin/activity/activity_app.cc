// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/activity_app.h"

#include <lib/zx/clock.h>
#include <zircon/status.h>

#include "src/lib/fxl/logging.h"

namespace activity {

std::vector<const ActivityControlConnection*> ActivityApp::control_bindings() const {
  std::vector<const ActivityControlConnection*> vec;
  vec.reserve(control_bindings_.size());
  for (const auto& entry : control_bindings_) {
    vec.push_back(entry.second.get());
  }
  return vec;
}

std::vector<const ActivityTrackerConnection*> ActivityApp::tracker_bindings() const {
  std::vector<const ActivityTrackerConnection*> vec;
  vec.reserve(tracker_bindings_.size());
  for (const auto& entry : tracker_bindings_) {
    vec.push_back(entry.second.get());
  }
  return vec;
}

std::vector<const ActivityProviderConnection*> ActivityApp::provider_bindings() const {
  std::vector<const ActivityProviderConnection*> vec;
  vec.reserve(provider_bindings_.size());
  for (const auto& entry : provider_bindings_) {
    vec.push_back(entry.second.get());
  }
  return vec;
}

void ActivityApp::AddControlBinding(
    fidl::InterfaceRequest<fuchsia::ui::activity::control::Control> request) {
  zx::unowned_channel unowned(request.channel());
  auto conn = std::make_unique<ActivityControlConnection>(state_machine_driver_.get(), dispatcher_,
                                                          std::move(request));
  conn->set_error_handler([this, unowned, cp = conn.get()](zx_status_t status) {
    auto entry = control_bindings_.find(unowned);
    if (entry != control_bindings_.end()) {
      control_bindings_.erase(entry);
    } else {
      FXL_LOG(ERROR) << "Failed to remove binding during cleanup";
    }
  });
  control_bindings_.emplace(std::move(unowned), std::move(conn));
}

void ActivityApp::AddTrackerBinding(
    fidl::InterfaceRequest<fuchsia::ui::activity::Tracker> request) {
  zx::unowned_channel unowned(request.channel());
  auto conn = std::make_unique<ActivityTrackerConnection>(state_machine_driver_.get(), dispatcher_,
                                                          std::move(request));
  conn->set_error_handler([this, unowned, cp = conn.get()](zx_status_t status) {
    cp->Stop();
    auto entry = tracker_bindings_.find(unowned);
    if (entry != tracker_bindings_.end()) {
      tracker_bindings_.erase(entry);
    } else {
      FXL_LOG(ERROR) << "Failed to remove binding during cleanup";
    }
  });
  tracker_bindings_.emplace(std::move(unowned), std::move(conn));
}

void ActivityApp::AddProviderBinding(
    fidl::InterfaceRequest<fuchsia::ui::activity::Provider> request) {
  zx::unowned_channel unowned(request.channel());
  auto conn = std::make_unique<ActivityProviderConnection>(state_machine_driver_.get(), dispatcher_,
                                                           std::move(request),
                                                           zx::clock::get_monotonic().get());
  conn->set_error_handler([this, unowned, cp = conn.get()](zx_status_t status) {
    if ((status = cp->Stop()) != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to clean up state on connection close";
    }
    auto entry = provider_bindings_.find(unowned);
    if (entry != provider_bindings_.end()) {
      provider_bindings_.erase(entry);
    } else {
      FXL_LOG(ERROR) << "Failed to remove binding during cleanup";
    }
  });
  provider_bindings_.emplace(std::move(unowned), std::move(conn));
}

}  // namespace activity
