// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thermal_test_control.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

ClientStateWatcher::ClientStateWatcher()
    : watcher_binding_(this), hanging_get_(nullptr), pending_client_state_(0) {}

void ClientStateWatcher::Bind(fidl::InterfaceRequest<fuchsia::thermal::ClientStateWatcher> watcher,
                              fit::function<void(zx_status_t)> error_handler) {
  watcher_binding_.Bind(std::move(watcher));
  watcher_binding_.set_error_handler(std::move(error_handler));
}

void ClientStateWatcher::Watch(fuchsia::thermal::ClientStateWatcher::WatchCallback callback) {
  hanging_get_ = std::move(callback);
  MaybeSendThermalState();
}

void ClientStateWatcher::SetThermalState(uint64_t thermal_state) {
  if (thermal_state != client_thermal_state_) {
    pending_client_state_ = thermal_state;
    MaybeSendThermalState();
  }
}

void ClientStateWatcher::MaybeSendThermalState() {
  if (hanging_get_ && pending_client_state_) {
    hanging_get_(*pending_client_state_);
    hanging_get_ = nullptr;

    client_thermal_state_ = *pending_client_state_;
    pending_client_state_.reset();
  }
}

ThermalTestControl::ThermalTestControl(std::unique_ptr<sys::ComponentContext> context)
    : context_(std::move(context)),
      client_state_connector_binding_(this),
      test_controller_binding_(this) {
  FX_LOGS(INFO) << "Creating ThermalTestControl";

  fidl::InterfaceRequestHandler<fuchsia::thermal::ClientStateConnector> connector_handler =
      [&](fidl::InterfaceRequest<fuchsia::thermal::ClientStateConnector> request) {
        client_state_connector_binding_.Bind(std::move(request));
      };
  context_->outgoing()->AddPublicService(std::move(connector_handler));

  fidl::InterfaceRequestHandler<::test::thermal::ClientStateControl> test_control_handler =
      [&](fidl::InterfaceRequest<::test::thermal::ClientStateControl> request) {
        test_controller_binding_.Bind(std::move(request));
      };
  context_->outgoing()->AddPublicService(std::move(test_control_handler));
}

void ThermalTestControl::Connect(
    std::string client_type, fidl::InterfaceRequest<fuchsia::thermal::ClientStateWatcher> watcher) {
  FX_LOGS(TRACE) << "client_type=" << client_type;

  ZX_ASSERT(!IsClientTypeConnectedInternal(client_type));

  watchers_[client_type] = std::make_unique<ClientStateWatcher>();
  watchers_[client_type]->Bind(std::move(watcher), [&, client_type](zx_status_t status) {
    ZX_ASSERT(watchers_.erase(client_type) == 1);
  });
}

void ThermalTestControl::IsClientTypeConnected(std::string client_type,
                                               IsClientTypeConnectedCallback callback) {
  callback(IsClientTypeConnectedInternal(client_type));
}

void ThermalTestControl::SetThermalState(std::string client_type, uint64_t state,
                                         SetThermalStateCallback callback) {
  FX_LOGS(TRACE) << "client_type=" << client_type << " state=" << state;

  ZX_ASSERT(IsClientTypeConnectedInternal(client_type));

  watchers_[client_type]->SetThermalState(state);
  callback();
}

bool ThermalTestControl::IsClientTypeConnectedInternal(std::string client_type) {
  return watchers_.find(client_type) != watchers_.end();
}
