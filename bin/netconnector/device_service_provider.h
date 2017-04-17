// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <string>

#include "application/services/service_provider.fidl.h"
#include "apps/netconnector/src/socket_address.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/macros.h"

namespace netconnector {

class NetConnectorImpl;

// Provides services on a remote device.
class DeviceServiceProvider : public app::ServiceProvider {
 public:
  static std::unique_ptr<DeviceServiceProvider> Create(
      const std::string& device_name,
      const SocketAddress& address,
      fidl::InterfaceRequest<app::ServiceProvider> request,
      NetConnectorImpl* owner);

  ~DeviceServiceProvider() override;

  void ConnectToService(const fidl::String& service_name,
                        mx::channel channel) override;

 private:
  DeviceServiceProvider(const std::string& device_name,
                        const SocketAddress& address,
                        fidl::InterfaceRequest<app::ServiceProvider> request,
                        NetConnectorImpl* owner);

  std::string device_name_;
  SocketAddress address_;
  fidl::Binding<app::ServiceProvider> binding_;
  NetConnectorImpl* owner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceServiceProvider);
};

}  // namespace netconnector
