// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_HELPERS_NETSTACK_INTERMEDIARY_NETSTACK_INTERMEDIARY_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_HELPERS_NETSTACK_INTERMEDIARY_NETSTACK_INTERMEDIARY_H_

#include <fuchsia/net/virtualization/cpp/fidl.h>
#include <fuchsia/netemul/network/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/scope.h>
#include <lib/sys/cpp/component_context.h>

#include <mutex>
#include <vector>

#include <src/connectivity/network/testing/netemul/lib/network/ethernet_client.h>

#include "src/lib/fxl/macros.h"

constexpr size_t kMacAddrStringLength = 17;

// NetstackIntermediary implements only the Netstack methods that are used by
// Machina guests. Rather than creating an ethernet device and associating it
// with an instance of Netstack, NetstackIntermediary bridges guests into the
// Netemul virtual network under test.
class NetstackIntermediary : public fuchsia::netstack::Netstack,
                             public fuchsia::net::virtualization::Control,
                             public fuchsia::net::virtualization::Network {
 public:
  using MacAddr = std::array<uint8_t, 6>;
  using NetworkMap = std::map<MacAddr, std::string>;
  using NetworkBinding = std::pair<std::unique_ptr<netemul::EthernetClient>,
                                   fidl::InterfacePtr<fuchsia::netemul::network::FakeEndpoint>>;

  explicit NetstackIntermediary(NetworkMap mac_network_mapping);

  // The following methods are required by the Machina guest's VirtioNet.
  void CreateNetwork(
      fuchsia::net::virtualization::Config config,
      fidl::InterfaceRequest<fuchsia::net::virtualization::Network> network) override;

  void AddPort(fidl::InterfaceHandle<::fuchsia::hardware::network::Port> port,
               fidl::InterfaceRequest<fuchsia::net::virtualization::Interface> interface) override;

  void AddEthernetDevice(std::string topological_path,
                         fuchsia::netstack::InterfaceConfig interfaceConfig,
                         ::fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device> device,
                         AddEthernetDeviceCallback callback) override;

  // The following methods are not used by Machina guests and are stubbed out.
  void GetDhcpClient(uint32_t nicid, ::fidl::InterfaceRequest<::fuchsia::net::dhcp::Client> client,
                     GetDhcpClientCallback callback) override {}

  void BridgeInterfaces(std::vector<uint32_t> nicids, BridgeInterfacesCallback callback) override {}

 protected:
  NetstackIntermediary(NetworkMap mac_network_mapping,
                       std::unique_ptr<sys::ComponentContext> context);

 private:
  fpromise::promise<fidl::InterfaceHandle<fuchsia::netemul::network::Network>, zx_status_t>
  GetNetwork(const MacAddr& octets);

  void ReadGuestEp(size_t index);

  std::vector<NetworkBinding> guest_client_endpoints_;
  NetworkMap mac_network_mapping_;

  std::unique_ptr<sys::ComponentContext> context_;
  async::Executor executor_;
  fpromise::scope scope_;

  fidl::BindingSet<fuchsia::netstack::Netstack> netstack_;
  fidl::BindingSet<fuchsia::net::virtualization::Control> control_;
  fidl::BindingSet<fuchsia::net::virtualization::Network> network_;
  uint64_t pending_writes_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(NetstackIntermediary);
};

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_HELPERS_NETSTACK_INTERMEDIARY_NETSTACK_INTERMEDIARY_H_
