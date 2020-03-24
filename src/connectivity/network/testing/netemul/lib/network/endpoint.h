// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_ENDPOINT_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_ENDPOINT_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <zircon/system/ulib/fit/include/lib/fit/function.h>

#include <memory>

#include "src/connectivity/network/testing/netemul/lib/network/consumer.h"

namespace netemul {
namespace impl {
class EndpointImpl;
}  // namespace impl

class NetworkContext;
class EndpointManager;
class Endpoint : public fuchsia::netemul::network::Endpoint,
                 public fuchsia::netemul::network::DeviceProxy {
 public:
  using Ptr = std::unique_ptr<Endpoint>;
  using FEndpoint = fuchsia::netemul::network::Endpoint;
  using FProxy = fuchsia::netemul::network::DeviceProxy;
  using Config = fuchsia::netemul::network::EndpointConfig;
  using EndpointClosedCallback = fit::function<void(const Endpoint&)>;
  using Backing = fuchsia::netemul::network::EndpointBacking;

  Endpoint(NetworkContext* context, std::string name, Config config);

  ~Endpoint();

  const std::string& name() const { return name_; }

  // Sets up the endpoint based on the configuration
  zx_status_t Startup(const NetworkContext& context, bool start_online);

  // fidl interface implementations:
  void GetConfig(GetConfigCallback callback) override;
  void GetName(GetNameCallback callback) override;
  void SetLinkUp(bool up, SetLinkUpCallback callback) override;
  void GetDevice(GetDeviceCallback callback) override;
  void GetProxy(fidl::InterfaceRequest<FProxy> proxy) override;

  void ServeDevice(zx::channel channel) override;

  // Installs a data sink on endpoint.
  // Data sinks will be notified via ->Consume of any data that is sent to the
  // endpoint.
  zx_status_t InstallSink(data::BusConsumer::Ptr sink, data::Consumer::Ptr* src);
  // Removes a data sink, if installed.
  zx_status_t RemoveSink(data::BusConsumer::Ptr sink, data::Consumer::Ptr* src);

  // Closed callback will be called whenever the endpoint dies due to:
  // a) Its backing has disappeared or
  // b) All FIDL bindings were closed.
  void SetClosedCallback(EndpointClosedCallback cb);

 protected:
  friend EndpointManager;

  void Bind(fidl::InterfaceRequest<FEndpoint> req);

 private:
  EndpointClosedCallback closed_callback_;
  std::unique_ptr<impl::EndpointImpl> impl_;
  // Pointer to parent context. Not owned.
  NetworkContext* parent_;
  std::string name_;
  fidl::BindingSet<FEndpoint> bindings_;
  fidl::BindingSet<FProxy> proxy_bindings_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_ENDPOINT_H_
