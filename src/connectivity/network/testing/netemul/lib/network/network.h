// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_H_

#include <fuchsia/net/virtualization/cpp/fidl.h>
#include <fuchsia/netemul/network/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <memory>
#include <unordered_map>

#include "src/connectivity/lib/network-device/cpp/network_device_client.h"
#include "src/connectivity/network/testing/netemul/lib/network/consumer.h"

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
  ~Network() override;

  const std::string& name() const { return name_; }

  // Attaches named endpoint to network
  zx_status_t AttachEndpoint(std::string name);

  // fidl interface implementations:
  void AddDevice(
      uint8_t port_id, fidl::InterfaceHandle<::fuchsia::hardware::network::Device> device,
      fidl::InterfaceRequest<fuchsia::net::virtualization::Interface> interface) override;
  void GetConfig(GetConfigCallback callback) override;
  void GetName(GetNameCallback callback) override;
  void SetConfig(fuchsia::netemul::network::NetworkConfig config,
                 SetConfigCallback callback) override;
  void AttachEndpoint(::std::string name, AttachEndpointCallback callback) override;
  void RemoveEndpoint(::std::string name, RemoveEndpointCallback callback) override;
  void CreateFakeEndpoint(
      fidl::InterfaceRequest<fuchsia::netemul::network::FakeEndpoint> ep) override;

  // ClosedCallback is called when all bindings to the service are gone
  void SetClosedCallback(ClosedCallback cb);

  // returns true if network config is valid.
  static bool CheckConfig(const Config& config);

 protected:
  friend NetworkManager;

  void Bind(fidl::InterfaceRequest<FNetwork> req);

 private:
  class Interface : public fuchsia::net::virtualization::Interface, public data::Consumer {
   public:
    explicit Interface(uint8_t port_id,
                       fidl::InterfaceRequest<fuchsia::net::virtualization::Interface> interface,
                       async_dispatcher_t* dispatcher,
                       fidl::ClientEnd<fuchsia_hardware_network::Device> device)
        : port_id_(port_id),
          binding_(this, std::move(interface), dispatcher),
          client_(std::move(device), dispatcher),
          weak_ptr_factory_(this) {}

    // The binding retains a pointer to |this|, so |this| must never move.
    Interface(Interface&&) = delete;
    Interface& operator=(Interface&&) = delete;

    fidl::Binding<fuchsia::net::virtualization::Interface>& binding() { return binding_; }
    network::client::NetworkDeviceClient& client() { return client_; }

    void Consume(const void* data, size_t len) override;

    fxl::WeakPtr<data::Consumer> GetPointer() { return weak_ptr_factory_.GetWeakPtr(); };

   private:
    const uint8_t port_id_;
    fidl::Binding<fuchsia::net::virtualization::Interface> binding_;
    network::client::NetworkDeviceClient client_;
    fxl::WeakPtrFactory<data::Consumer> weak_ptr_factory_;
  };

  ClosedCallback closed_callback_;
  std::shared_ptr<impl::NetworkBus> bus_;
  // Pointer to parent context. Not owned.
  NetworkContext* parent_;
  std::string name_;
  Config config_;
  fidl::BindingSet<FNetwork> bindings_;
  std::unordered_map<zx_handle_t, Interface> guests_;
};

}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_LIB_NETWORK_NETWORK_H_
