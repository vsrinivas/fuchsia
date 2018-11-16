// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_NETEMUL_NETWORK_NETWORK_H_
#define LIB_NETEMUL_NETWORK_NETWORK_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <memory>

namespace netemul {
namespace impl {
class NetworkBus;
}
class NetworkContext;
class NetworkManager;
class Network : public fuchsia::netemul::network::Network {
 public:
  using FNetwork = fuchsia::netemul::network::Network;
  using Config = fuchsia::netemul::network::NetworkConfig;
  using Ptr = std::unique_ptr<Network>;
  using ClosedCallback = fit::function<void(const Network&)>;

  Network(NetworkContext* context, std::string name, Config config);
  ~Network();

  const std::string& name() const { return name_; }

  // fidl interface implementations:
  void GetConfig(GetConfigCallback callback) override;
  void GetName(GetNameCallback callback) override;
  void SetConfig(fuchsia::netemul::network::NetworkConfig config,
                 SetConfigCallback callback) override;
  void AttachEndpoint(::fidl::StringPtr name,
                      AttachEndpointCallback callback) override;
  void RemoveEndpoint(::fidl::StringPtr name,
                      RemoveEndpointCallback callback) override;
  void CreateFakeEndpoint(
      fidl::InterfaceRequest<fuchsia::netemul::network::FakeEndpoint> ep)
      override;

  // ClosedCallback is called when all bindings to the service are gone
  void SetClosedCallback(ClosedCallback cb);

 protected:
  friend NetworkManager;

  fidl::InterfaceHandle<FNetwork> Bind();

 private:
  ClosedCallback closed_callback_;
  std::shared_ptr<impl::NetworkBus> bus_;
  // Pointer to parent context. Not owned.
  NetworkContext* parent_;
  std::string name_;
  Config config_;
  fidl::BindingSet<FNetwork> bindings_;
};

}  // namespace netemul

#endif  // LIB_NETEMUL_NETWORK_NETWORK_H_
