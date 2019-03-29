// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETCONNECTOR_DEVICE_SERVICE_PROVIDER_H_
#define GARNET_BIN_NETCONNECTOR_DEVICE_SERVICE_PROVIDER_H_

#include <memory>
#include <string>

#include <fuchsia/sys/cpp/fidl.h>

#include "garnet/lib/inet/socket_address.h"
#include "lib/fidl/cpp/binding.h"
#include "src/lib/fxl/macros.h"

namespace netconnector {

class NetConnectorImpl;

// Provides services on a remote device.
class DeviceServiceProvider : public fuchsia::sys::ServiceProvider {
 public:
  static std::unique_ptr<DeviceServiceProvider> Create(
      const std::string& device_name, const inet::SocketAddress& address,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request,
      NetConnectorImpl* owner);

  ~DeviceServiceProvider() override;

  void ConnectToService(std::string service_name,
                        zx::channel channel) override;

 private:
  DeviceServiceProvider(
      const std::string& device_name, const inet::SocketAddress& address,
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request,
      NetConnectorImpl* owner);

  std::string device_name_;
  inet::SocketAddress address_;
  fidl::Binding<fuchsia::sys::ServiceProvider> binding_;
  NetConnectorImpl* owner_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DeviceServiceProvider);
};

}  // namespace netconnector

#endif  // GARNET_BIN_NETCONNECTOR_DEVICE_SERVICE_PROVIDER_H_
