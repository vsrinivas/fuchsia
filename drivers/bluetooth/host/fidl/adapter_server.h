// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include <bluetooth_control/cpp/fidl.h>
#include <bluetooth_host/cpp/fidl.h>

#include "garnet/drivers/bluetooth/host/fidl/server_base.h"

namespace btlib {
namespace gap {

class BrEdrDiscoverySession;
class BrEdrDiscoverableSession;
class LowEnergyDiscoverySession;
class RemoteDevice;

}  // namespace gap
}  // namespace btlib

namespace bthost {

// Implements the control.Adapter FIDL interface.
class AdapterServer : public AdapterServerBase<::bluetooth_host::Adapter> {
 public:
  AdapterServer(fxl::WeakPtr<::btlib::gap::Adapter> adapter,
                fidl::InterfaceRequest<bluetooth_host::Adapter> request);
  ~AdapterServer() override;

 private:
  // ::bluetooth_control::Adapter overrides:
  void GetInfo(GetInfoCallback callback) override;
  void SetLocalName(::fidl::StringPtr local_name,
                    SetLocalNameCallback callback) override;
  void StartDiscovery(StartDiscoveryCallback callback) override;
  void StopDiscovery(StopDiscoveryCallback callback) override;
  void SetConnectable(bool connectable,
                      SetConnectableCallback callback) override;
  void SetDiscoverable(bool discoverable,
                       SetDiscoverableCallback callback) override;

  // Called by |le_discovery_session_| when devices are discovered.
  void OnDiscoveryResult(const ::btlib::gap::RemoteDevice& remote_device);

  // The currently active discovery sessions.
  // These are non-null when a client requests to perform discovery.
  bool requesting_discovery_;
  std::unique_ptr<::btlib::gap::LowEnergyDiscoverySession>
      le_discovery_session_;
  std::unique_ptr<::btlib::gap::BrEdrDiscoverySession> bredr_discovery_session_;

  // The currently active discoverable/advertising sessions.
  // These are non-null when a client requests that the device is discoverable.
  // TODO(NET-830): Enable connectable LE advertising
  bool requesting_discoverable_;
  std::unique_ptr<::btlib::gap::BrEdrDiscoverableSession>
      bredr_discoverable_session_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<AdapterServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AdapterServer);
};

}  // namespace bthost
