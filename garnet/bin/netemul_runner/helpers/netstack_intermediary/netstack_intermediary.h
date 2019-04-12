// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_HELPERS_NETSTACK_INTERMEDIARY_NETSTACK_INTERMEDIARY_H_
#define GARNET_BIN_NETEMUL_RUNNER_HELPERS_NETSTACK_INTERMEDIARY_NETSTACK_INTERMEDIARY_H_

#include <fuchsia/netemul/network/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <garnet/public/lib/netemul/network/ethernet_client.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async_promise/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/promise.h>
#include <lib/fit/scope.h>
#include <lib/sys/cpp/component_context.h>

#include <mutex>
#include <vector>

#include "src/lib/fxl/macros.h"

// NetstackIntermediary implements only the Netstack methods that are used by
// Machina guests. Rather than creating an ethernet device and associating it
// with an instance of Netstack, NetstackIntermediary bridges guests into the
// Netemul virtual network under test.
class NetstackIntermediary : public fuchsia::netstack::Netstack {
 public:
  NetstackIntermediary(std::string network_name);

  // The following methods are required by the Machina guest's VirtioNet.
  void AddEthernetDevice(
      std::string topological_path,
      fuchsia::netstack::InterfaceConfig interfaceConfig,
      ::fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device> device,
      AddEthernetDeviceCallback callback) override;

  void SetInterfaceAddress(uint32_t nicid, fuchsia::net::IpAddress addr,
                           uint8_t prefixLen,
                           SetInterfaceAddressCallback callback) override;

  void SetInterfaceStatus(uint32_t nicid, bool enabled) override {}

  // The following methods are not used by Machina guests and are stubbed out.
  void GetPortForService(std::string service,
                         fuchsia::netstack::Protocol protocol,
                         GetPortForServiceCallback callback) override {}

  void GetAddress(std::string address, uint16_t port,
                  GetAddressCallback callback) override {}

  void GetInterfaces(GetInterfacesCallback callback) override {}
  void GetInterfaces2(GetInterfaces2Callback callback) override {}

  void GetRouteTable(GetRouteTableCallback callback) override {}
  void GetRouteTable2(GetRouteTable2Callback callback) override {}

  void GetStats(uint32_t nicid, GetStatsCallback callback) override {}

  void GetAggregateStats(
      ::fidl::InterfaceRequest<::fuchsia::io::Node> object) override {}

  void RemoveInterfaceAddress(
      uint32_t nicid, fuchsia::net::IpAddress addr, uint8_t prefixLen,
      RemoveInterfaceAddressCallback callback) override {}

  void SetInterfaceMetric(uint32_t nicid, uint32_t metric,
                          SetInterfaceMetricCallback callback) override {}

  void SetDhcpClientStatus(uint32_t nicid, bool enabled,
                           SetDhcpClientStatusCallback callback) override {}

  void BridgeInterfaces(std::vector<uint32_t> nicids,
                        BridgeInterfacesCallback callback) override {}

  void StartRouteTableTransaction(
      ::fidl::InterfaceRequest<fuchsia::netstack::RouteTableTransaction>
          routeTableTransaction,
      StartRouteTableTransactionCallback callback) override {}

  fidl::InterfaceRequestHandler<fuchsia::netstack::Netstack> GetHandler() {
    return bindings_.GetHandler(this);
  }

 protected:
  NetstackIntermediary(std::string network_name,
                       std::unique_ptr<sys::ComponentContext> context);

 private:
  fit::promise<fidl::InterfaceHandle<fuchsia::netemul::network::Network>>
  GetNetwork(std::string network_name);
  fit::promise<zx_status_t> SetupEthClient(
      fidl::InterfaceHandle<fuchsia::netemul::network::Network> net);

  std::string network_name_;
  std::unique_ptr<netemul::EthernetClient> eth_client_;
  fidl::InterfacePtr<fuchsia::netemul::network::FakeEndpoint> fake_ep_;

  std::unique_ptr<sys::ComponentContext> context_;
  async::Executor executor_;
  fit::scope scope_;

  fidl::BindingSet<fuchsia::netstack::Netstack> bindings_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(NetstackIntermediary);
};

#endif  // GARNET_BIN_NETEMUL_RUNNER_HELPERS_NETSTACK_INTERMEDIARY_NETSTACK_INTERMEDIARY_H_
