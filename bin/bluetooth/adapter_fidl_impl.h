// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/bluetooth/service/interfaces/control.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/weak_ptr.h"

namespace bluetooth {
namespace gap {

class Adapter;

}  // namespace gap
}  // namespace bluetooth

namespace bluetooth_service {

// FidlImplements the Adapter FIDL interface.
class AdapterFidlImpl : public ::bluetooth::control::Adapter {
 public:
  using ConnectionErrorHandler = std::function<void(AdapterFidlImpl*)>;
  AdapterFidlImpl(const ftl::WeakPtr<::bluetooth::gap::Adapter>& adapter,
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

  // The underlying Adapter object.
  ftl::WeakPtr<::bluetooth::gap::Adapter> adapter_;

  // The interface binding that represents the connection to the client application.
  ::fidl::Binding<::bluetooth::control::Adapter> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(AdapterFidlImpl);
};

}  // namespace bluetooth_service
