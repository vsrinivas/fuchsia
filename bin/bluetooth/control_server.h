// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <memory>
#include <set>

#include <fuchsia/cpp/bluetooth_control.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

#include "bluetooth_manager.h"

namespace bluetooth_service {

class BluetoothManager;

class ControlServer : public bluetooth_control::Control {
 public:
  using ConnectionErrorHandler = std::function<void(ControlServer*)>;
  ControlServer(::bluetooth_service::BluetoothManager* bluetooth_manager,
                fidl::InterfaceRequest<::bluetooth_control::Control> request,
                const ConnectionErrorHandler& connection_error_handler);
  ~ControlServer() override = default;

  // Methods for notifying the delegates.
  void NotifyActiveAdapterChanged(
      const bluetooth_control::AdapterInfoPtr& adapter_ptr);
  void NotifyAdapterUpdated(
      const bluetooth_control::AdapterInfoPtr& adapter_ptr);
  void NotifyAdapterRemoved(const std::string& adapter_id);
  void NotifyRemoteDeviceUpdated(const bluetooth_control::RemoteDevice& device);

 private:
  // ::bluetooth_control::AdapterManager overrides:
  void IsBluetoothAvailable(IsBluetoothAvailableCallback callback) override;
  void SetDelegate(fidl::InterfaceHandle<::bluetooth_control::ControlDelegate>
                       delegate) override;
  void SetPairingDelegate(
      bluetooth_control::InputCapabilityType in,
      bluetooth_control::OutputCapabilityType out,
      fidl::InterfaceHandle<::bluetooth_control::PairingDelegate> delegate)
      override;
  void GetAdapters(GetAdaptersCallback callback) override;
  void SetActiveAdapter(fidl::StringPtr identifier,
                        SetActiveAdapterCallback callback) override;
  void GetActiveAdapterInfo(GetActiveAdapterInfoCallback callback) override;
  void SetRemoteDeviceDelegate(
      fidl::InterfaceHandle<::bluetooth_control::RemoteDeviceDelegate> delegate,
      bool include_rssi) override;
  void GetKnownRemoteDevices(GetKnownRemoteDevicesCallback callback) override;
  void SetName(fidl::StringPtr name, SetNameCallback callback) override;
  void SetDiscoverable(bool discoverable,
                       SetDiscoverableCallback callback) override;
  void Connect(fidl::StringPtr device_id,
               bool bond,
               ConnectCallback callback) override;
  void Disconnect(fidl::StringPtr device_id,
                  DisconnectCallback callback) override;
  void Forget(fidl::StringPtr device_id, ForgetCallback callback) override;

  void RequestDiscovery(bool discovering,
                        RequestDiscoveryCallback callback) override;

  // The underlying BluetoothManager. This is expected to outlive this instance.
  ::bluetooth_service::BluetoothManager* bluetooth_manager_;  // weak

  // The interface binding that represents the connection to the client
  // application.
  fidl::Binding<::bluetooth_control::Control> binding_;

  // The delegate that is set via SetDelegate().
  ::bluetooth_control::ControlDelegatePtr delegate_;

  // The pairing delegate that is set via SetPairingDelegate().
  ::bluetooth_control::PairingDelegatePtr pairing_delegate_;

  // The remote device delegate set via SetRemoteDeviceDelegate().
  ::bluetooth_control::RemoteDeviceDelegatePtr device_delegate_;

  // A token that we hold when we are requesting discovery.
  std::unique_ptr<::bluetooth_service::DiscoveryRequestToken> discovery_token_;

  fxl::WeakPtrFactory<ControlServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ControlServer);
};

}  // namespace bluetooth_service
