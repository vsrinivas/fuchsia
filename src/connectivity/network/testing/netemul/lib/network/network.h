// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_H_

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

  // Attaches named endpoint to network
  zx_status_t AttachEndpoint(std::string name);

  // fidl interface implementations:
  void GetConfig(GetConfigCallback callback) override;
  void GetName(GetNameCallback callback) override;
  void SetConfig(fuchsia::netemul::network::NetworkConfig config,
                 SetConfigCallback callback) override;
  void AttachEndpoint(::std::string name,
                      AttachEndpointCallback callback) override;
  void RemoveEndpoint(::std::string name,
                      RemoveEndpointCallback callback) override;
  void CreateFakeEndpoint(
      fidl::InterfaceRequest<fuchsia::netemul::network::FakeEndpoint> ep)
      override;

  // ClosedCallback is called when all bindings to the service are gone
  void SetClosedCallback(ClosedCallback cb);

  // returns true if network config is valid.
  static bool CheckConfig(const Config& config);

 protected:
  friend NetworkManager;

  void Bind(fidl::InterfaceRequest<FNetwork> req);

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

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_H_
