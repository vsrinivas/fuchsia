// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thermal_test_control.h"

#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

LegacyControllerImpl::LegacyControllerImpl(std::unique_ptr<sys::ComponentContext>& context)
    : thermal_controller_binding_(this), test_controller_binding_(this) {
  fidl::InterfaceRequestHandler<fuchsia::thermal::Controller> legacy_controller_handler =
      [&](fidl::InterfaceRequest<fuchsia::thermal::Controller> request) {
        thermal_controller_binding_.Bind(std::move(request));
      };
  context->outgoing()->AddPublicService(std::move(legacy_controller_handler));

  fidl::InterfaceRequestHandler<::test::thermal::Control> legacy_test_control_handler =
      [&](fidl::InterfaceRequest<::test::thermal::Control> request) {
        test_controller_binding_.Bind(std::move(request));
      };
  context->outgoing()->AddPublicService(std::move(legacy_test_control_handler));
}

void LegacyControllerImpl::Subscribe(fidl::InterfaceHandle<fuchsia::thermal::Actor> actor,
                                     fuchsia::thermal::ActorType actor_type,
                                     std::vector<fuchsia::thermal::TripPoint> trip_points,
                                     SubscribeCallback callback) {
  FX_LOGS(TRACE) << "actor_type " << static_cast<uint32_t>(actor_type) << ", trip_points["
                 << trip_points.size() << "]";
  for (auto i = 0u; i < trip_points.size(); ++i) {
    FX_LOGS(TRACE) << "  point[" << i << "]:";
    FX_LOGS(TRACE) << "    deactivate_below:" << trip_points[i].deactivate_below;
    FX_LOGS(TRACE) << "    activate_at:" << trip_points[i].activate_at;
  }

  auto interface = actor.Bind();
  interface.set_error_handler([](zx_status_t status) {
    FX_PLOGS(ERROR, status) << "fuchsia::thermal::Actor disconnected";
  });
  subscribers_.push_back({
      .actor = std::move(interface),
      .type = actor_type,
      .points = std::move(trip_points),
  });

  callback({fpromise::ok()});
}

// For each thermal subscriber, return its type and number of supported thermal states.
void LegacyControllerImpl::GetSubscriberInfo(GetSubscriberInfoCallback callback) {
  std::vector<::test::thermal::SubscriberInfo> subscribers;

  for (auto i = 0u; i < subscribers_.size(); ++i) {
    subscribers.push_back({
        .actor_type = subscribers_[i].type,
        // All subscribers support state 0; 'N' trip_points implies 'N+1' thermal states.
        .num_thermal_states = static_cast<uint32_t>(subscribers_[i].points.size() + 1),
    });
  }

  callback(std::move(subscribers));
}

void LegacyControllerImpl::SetThermalState(uint32_t subscriber_index, uint32_t state,
                                           SetThermalStateCallback callback) {
  FX_CHECK(subscriber_index < subscribers_.size())
      << "Subscriber index out of range (requested " << subscriber_index << " > max "
      << subscribers_.size() - 1 << ")";

  FX_CHECK(state <= subscribers_[subscriber_index].points.size())
      << "Thermal state out of range (requested " << state << " > max "
      << subscribers_[subscriber_index].points.size() - 1 << ")";

  subscribers_[subscriber_index].actor->SetThermalState(state,
                                                        [cbk = std::move(callback)]() { cbk(); });
}

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

  legacy_controller_impl_ = std::make_unique<LegacyControllerImpl>(context_);
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
