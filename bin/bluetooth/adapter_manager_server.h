// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "lib/bluetooth/fidl/control.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace bluetooth_service {

class Adapter;
class AdapterManager;

class AdapterManagerServer : public bluetooth::control::AdapterManager {
 public:
  // |app| is the App object that created and owns this instance. |app| MUST
  // out-live this instance.
  using ConnectionErrorHandler = std::function<void(AdapterManagerServer*)>;
  AdapterManagerServer(
      ::bluetooth_service::AdapterManager* adapter_manager,
      fidl::InterfaceRequest<::bluetooth::control::AdapterManager> request,
      const ConnectionErrorHandler& connection_error_handler);
  ~AdapterManagerServer() override = default;

  // Methods for notifying the delegate.
  void NotifyActiveAdapterChanged(const Adapter* adapter);
  void NotifyAdapterAdded(const Adapter& adapter);
  void NotifyAdapterRemoved(const Adapter& adapter);

 private:
  // ::bluetooth::control::AdapterManager overrides:
  void IsBluetoothAvailable(
      const IsBluetoothAvailableCallback& callback) override;
  void SetDelegate(
      fidl::InterfaceHandle<::bluetooth::control::AdapterManagerDelegate>
          delegate) override;
  void ListAdapters(const ListAdaptersCallback& callback) override;
  void SetActiveAdapter(const fidl::String& identifier,
                        const SetActiveAdapterCallback& callback) override;
  void GetActiveAdapter(
      fidl::InterfaceRequest<::bluetooth::control::Adapter> adapter) override;

  // The underlying AdapterManager. This is expected to outlive this instance.
  ::bluetooth_service::AdapterManager* adapter_manager_;  // weak

  // The interface binding that represents the connection to the client
  // application.
  fidl::Binding<::bluetooth::control::AdapterManager> binding_;

  // The delegate that is set via SetDelegate().
  ::bluetooth::control::AdapterManagerDelegatePtr delegate_;

  fxl::WeakPtrFactory<AdapterManagerServer> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AdapterManagerServer);
};

}  // namespace bluetooth_service
