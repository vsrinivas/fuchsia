// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/bluetooth/lib/gap/low_energy_discovery_manager.h"
#include "apps/bluetooth/service/interfaces/control.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth {
namespace gap {

class Adapter;
class RemoteDevice;

}  // namespace gap
}  // namespace bluetooth

namespace bluetooth_service {

// FidlImplements the Adapter FIDL interface.
class AdapterFidlImpl : public ::bluetooth::control::Adapter {
 public:
  using ConnectionErrorHandler = std::function<void(AdapterFidlImpl*)>;
  AdapterFidlImpl(const fxl::WeakPtr<::bluetooth::gap::Adapter>& adapter,
                  ::fidl::InterfaceRequest<::bluetooth::control::Adapter> request,
                  const ConnectionErrorHandler& connection_error_handler);
  ~AdapterFidlImpl() override = default;

 private:
  // ::bluetooth::control::Adapter overrides:
  void GetInfo(const GetInfoCallback& callback) override;
  void SetDelegate(
      ::fidl::InterfaceHandle<::bluetooth::control::AdapterDelegate> delegate) override;
  void SetLocalName(const ::fidl::String& local_name, const ::fidl::String& shortened_local_name,
                    const SetLocalNameCallback& callback) override;
  void SetPowered(bool powered, const SetPoweredCallback& callback) override;
  void StartDiscovery(const StartDiscoveryCallback& callback) override;
  void StopDiscovery(const StopDiscoveryCallback& callback) override;

  // Called by |le_discovery_session_| when devices are discovered.
  void OnDiscoveryResult(const ::bluetooth::gap::RemoteDevice& remote_device);

  // Notifies the delegate that the Adapter's "discovering" state changed.
  void NotifyDiscoveringChanged();

  // The underlying Adapter object.
  fxl::WeakPtr<::bluetooth::gap::Adapter> adapter_;

  // The currently active LE discovery session. This is initialized when a client requests to
  // perform discovery.
  bool requesting_discovery_;
  std::unique_ptr<::bluetooth::gap::LowEnergyDiscoverySession> le_discovery_session_;

  // The interface binding that represents the connection to the client application.
  ::fidl::Binding<::bluetooth::control::Adapter> binding_;

  // The delegate that was set via SetDelegate().
  ::bluetooth::control::AdapterDelegatePtr delegate_;

  // Keep this as the last member to make sure that all weak pointers are invalidated before other
  // members get destroyed.
  fxl::WeakPtrFactory<AdapterFidlImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AdapterFidlImpl);
};

}  // namespace bluetooth_service
