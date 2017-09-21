// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <vector>

#include "lib/bluetooth/fidl/control.fidl.h"
#include "garnet/bin/bluetooth/adapter_fidl_impl.h"
#include "garnet/bin/bluetooth/adapter_manager.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"

namespace bluetooth_service {

class App;

// FidlImplements the AdapterManager FIDL interface.
class AdapterManagerFidlImpl : public ::bluetooth::control::AdapterManager,
                               public AdapterManager::Observer {
 public:
  // |app| is the App object that created and owns this instance. |app| MUST out-live this instance.
  using ConnectionErrorHandler = std::function<void(AdapterManagerFidlImpl*)>;
  AdapterManagerFidlImpl(App* app,
                         ::fidl::InterfaceRequest<::bluetooth::control::AdapterManager> request,
                         const ConnectionErrorHandler& connection_error_handler);
  ~AdapterManagerFidlImpl() override;

 private:
  // ::bluetooth::control::AdapterManager overrides:
  void IsBluetoothAvailable(const IsBluetoothAvailableCallback& callback) override;
  void SetDelegate(
      ::fidl::InterfaceHandle<::bluetooth::control::AdapterManagerDelegate> delegate) override;
  void GetAdapters(const GetAdaptersCallback& callback) override;
  void GetAdapter(const ::fidl::String& identifier,
                  ::fidl::InterfaceRequest<::bluetooth::control::Adapter> adapter) override;
  void SetActiveAdapter(const ::fidl::String& identifier,
                        const SetActiveAdapterCallback& callback) override;
  void GetActiveAdapter(::fidl::InterfaceRequest<::bluetooth::control::Adapter> adapter) override;

  // AdapterManager::Delegate overrides:
  void OnActiveAdapterChanged(bluetooth::gap::Adapter* adapter) override;
  void OnAdapterCreated(bluetooth::gap::Adapter* adapter) override;
  void OnAdapterRemoved(bluetooth::gap::Adapter* adapter) override;

  // Called when a AdapterFidlImpl that we own notifies its connection error handler.
  void OnAdapterFidlImplDisconnected(AdapterFidlImpl* adapter_fidl_impl);

  // Creates an AdapterFidlImpl for |adapter| and binds it to |request|.
  void CreateAdapterFidlImpl(fxl::WeakPtr<bluetooth::gap::Adapter> adapter,
                             ::fidl::InterfaceRequest<::bluetooth::control::Adapter> request);

  // The App instance that owns us. We keep a raw pointer as we expect |app_| to outlive us.
  App* app_;  // weak

  // The interface binding that represents the connection to the client application.
  ::fidl::Binding<::bluetooth::control::AdapterManager> binding_;

  // The Adapter FIDL interface handles that have been vended out by this AdapterManagerFidlImpl.
  std::vector<std::unique_ptr<AdapterFidlImpl>> adapter_fidl_impls_;

  // The delegate that is set via SetDelegate().
  ::bluetooth::control::AdapterManagerDelegatePtr delegate_;

  FXL_DISALLOW_COPY_AND_ASSIGN(AdapterManagerFidlImpl);
};

}  // namespace bluetooth_service
