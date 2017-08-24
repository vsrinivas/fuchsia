// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <unordered_map>

#include "apps/bluetooth/lib/gap/low_energy_connection_manager.h"
#include "apps/bluetooth/lib/gap/low_energy_discovery_manager.h"
#include "apps/bluetooth/service/interfaces/low_energy.fidl.h"
#include "apps/bluetooth/service/src/adapter_manager.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"

namespace bluetooth_service {

// Implements the low_energy::Central FIDL interface.
class LowEnergyCentralFidlImpl : public ::bluetooth::low_energy::Central,
                                 public AdapterManager::Observer {
 public:
  // |adapter_manager| is used to lazily request a handle to the corresponding adapter. It MUST
  // out-live this LowEnergyCentralFidlImpl instance.
  using ConnectionErrorHandler = std::function<void(LowEnergyCentralFidlImpl*)>;
  LowEnergyCentralFidlImpl(AdapterManager* adapter_manager,
                           ::fidl::InterfaceRequest<::bluetooth::low_energy::Central> request,
                           const ConnectionErrorHandler& connection_error_handler);
  ~LowEnergyCentralFidlImpl() override;

 private:
  // ::bluetooth::low_energy::Central overrides:
  void SetDelegate(
      ::fidl::InterfaceHandle<::bluetooth::low_energy::CentralDelegate> delegate) override;
  void GetPeripherals(::fidl::Array<::fidl::String> service_uuids,
                      const GetPeripheralsCallback& callback) override;
  void GetPeripheral(const ::fidl::String& identifier,
                     const GetPeripheralCallback& callback) override;
  void StartScan(::bluetooth::low_energy::ScanFilterPtr filter,
                 const StartScanCallback& callback) override;
  void StopScan() override;
  void ConnectPeripheral(const ::fidl::String& identifier,
                         const ConnectPeripheralCallback& callback) override;
  void DisconnectPeripheral(const ::fidl::String& identifier,
                            const DisconnectPeripheralCallback& callback) override;

  // AdapterManager::Delegate overrides:
  void OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) override;

  // Called by |scan_session_| when a device is discovered.
  void OnScanResult(const ::bluetooth::gap::RemoteDevice& remote_device);

  // Notifies the delegate that the scan state for this Central has changed.
  void NotifyScanStateChanged(bool scanning);

  // Notifies the delegate that the device with the given identifier has been disconnected.
  void NotifyPeripheralDisconnected(const std::string& identifier);

  // We keep a raw pointer as we expect this to outlive us.
  AdapterManager* adapter_manager_;  // weak

  // The currently active LE discovery session. This is initialized when a client requests to
  // perform a scan.
  bool requesting_scan_;
  std::unique_ptr<::bluetooth::gap::LowEnergyDiscoverySession> scan_session_;

  // This client's connection references. A client can hold a connection to multiple peers.
  // Each key is a remote device identifier. Each value is
  //   a. nullptr, if a connect request to this device is currently pending.
  //   b. a valid reference if this Central is holding a connection reference to this device.
  std::unordered_map<std::string, ::bluetooth::gap::LowEnergyConnectionRefPtr> connections_;

  // The interface binding that represents the connection to the client application.
  ::fidl::Binding<::bluetooth::low_energy::Central> binding_;

  // The delegate that is set via SetDelegate()
  ::bluetooth::low_energy::CentralDelegatePtr delegate_;

  // Keep this as the last member to make sure that all weak pointers are invalidated before other
  // members get destroyed.
  fxl::WeakPtrFactory<LowEnergyCentralFidlImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyCentralFidlImpl);
};

}  // namespace bluetooth_service
