// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_POWER_TESTING_THERMAL_TEST_CONTROL_THERMAL_TEST_CONTROL_H_
#define SRC_POWER_TESTING_THERMAL_TEST_CONTROL_THERMAL_TEST_CONTROL_H_

#include <fuchsia/thermal/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>

#include <test/thermal/cpp/fidl.h>

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

  fidl::Binding<fuchsia::thermal::ClientStateConnector> client_state_connector_binding_;
  fidl::Binding<test::thermal::ClientStateControl> test_controller_binding_;
  std::map<std::string, std::unique_ptr<ClientStateWatcher>> watchers_;
};

#endif  // SRC_POWER_TESTING_THERMAL_TEST_CONTROL_THERMAL_TEST_CONTROL_H_
