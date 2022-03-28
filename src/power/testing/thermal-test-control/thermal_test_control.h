// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_POWER_TESTING_THERMAL_TEST_CONTROL_THERMAL_TEST_CONTROL_H_
#define SRC_POWER_TESTING_THERMAL_TEST_CONTROL_THERMAL_TEST_CONTROL_H_

#include <fuchsia/thermal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include <test/thermal/cpp/fidl.h>

class LegacyControllerImpl : public fuchsia::thermal::Controller, public ::test::thermal::Control {
 public:
  explicit LegacyControllerImpl(std::unique_ptr<sys::ComponentContext>& context);
  void Subscribe(fidl::InterfaceHandle<fuchsia::thermal::Actor> actor,
                 fuchsia::thermal::ActorType actor_type,
                 std::vector<fuchsia::thermal::TripPoint> trip_points,
                 SubscribeCallback callback) override;
  void GetSubscriberInfo(GetSubscriberInfoCallback callback) override;
  void SetThermalState(uint32_t subscriber_index, uint32_t state,
                       SetThermalStateCallback callback) override;

 private:
  struct Subscriber {
    fidl::InterfacePtr<fuchsia::thermal::Actor> actor;
    fuchsia::thermal::ActorType type;
    std::vector<fuchsia::thermal::TripPoint> points;
  };

  fidl::Binding<fuchsia::thermal::Controller> thermal_controller_binding_;
  fidl::Binding<test::thermal::Control> test_controller_binding_;
  std::vector<Subscriber> subscribers_;
};

class ClientStateWatcher : public fuchsia::thermal::ClientStateWatcher {
 public:
  ClientStateWatcher();

 private:
  friend class ThermalTestControl;

  void Bind(fidl::InterfaceRequest<fuchsia::thermal::ClientStateWatcher> watcher,
            fit::function<void(zx_status_t)> error_handler);
  void Watch(fuchsia::thermal::ClientStateWatcher::WatchCallback callback) override;
  void SetThermalState(uint64_t thermal_state);
  void MaybeSendThermalState();

  fidl::Binding<fuchsia::thermal::ClientStateWatcher> watcher_binding_;
  fuchsia::thermal::ClientStateWatcher::WatchCallback hanging_get_;
  std::optional<uint64_t> client_thermal_state_;
  std::optional<uint64_t> pending_client_state_;
};

class ThermalTestControl : public fuchsia::thermal::ClientStateConnector,
                           public ::test::thermal::ClientStateControl {
 public:
  explicit ThermalTestControl(std::unique_ptr<sys::ComponentContext> context);

 private:
  // fuchsia.thermal.ClientStateConnector impl
  void Connect(std::string client_type,
               fidl::InterfaceRequest<fuchsia::thermal::ClientStateWatcher> watcher) override;

  // fuchsia.thermal.ClientStateControl impl
  void IsClientTypeConnected(std::string client_type,
                             IsClientTypeConnectedCallback callback) override;
  void SetThermalState(std::string client_type, uint64_t state,
                       SetThermalStateCallback callback) override;

  bool IsClientTypeConnectedInternal(std::string client_type);

  std::unique_ptr<sys::ComponentContext> context_;

  // TODO(fxbug.dev/96172): Remove this legacy controller implementation after AudioCore moves to
  // the new ClientStateController.
  std::unique_ptr<LegacyControllerImpl> legacy_controller_impl_;

  fidl::Binding<fuchsia::thermal::ClientStateConnector> client_state_connector_binding_;
  fidl::Binding<test::thermal::ClientStateControl> test_controller_binding_;
  std::map<std::string, std::unique_ptr<ClientStateWatcher>> watchers_;
};

#endif  // SRC_POWER_TESTING_THERMAL_TEST_CONTROL_THERMAL_TEST_CONTROL_H_
