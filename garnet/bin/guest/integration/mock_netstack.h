// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_INTEGRATION_MOCK_NETSTACK_H_
#define GARNET_BIN_GUEST_INTEGRATION_MOCK_NETSTACK_H_

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

static constexpr zx::duration kTestTimeout = zx::sec(15);

class MockNetstack : public fuchsia::netstack::Netstack {
 public:
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

  void SetInterfaceStatus(uint32_t nicid, bool enabled) override {}

  void SetInterfaceAddress(uint32_t nicid, fuchsia::net::IpAddress addr,
                           uint8_t prefixLen,
                           SetInterfaceAddressCallback callback) override;

  void RemoveInterfaceAddress(
      uint32_t nicid, fuchsia::net::IpAddress addr, uint8_t prefixLen,
      RemoveInterfaceAddressCallback callback) override {}

  void SetInterfaceMetric(uint32_t nicid, uint32_t metric,
                          SetInterfaceMetricCallback callback) override {}

  void SetDhcpClientStatus(uint32_t nicid, bool enabled,
                           SetDhcpClientStatusCallback callback) override {}

  void BridgeInterfaces(std::vector<uint32_t> nicids,
                        BridgeInterfacesCallback callback) override {}

  void AddEthernetDevice(
      std::string topological_path,
      fuchsia::netstack::InterfaceConfig interfaceConfig,
      ::fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device> device,
      AddEthernetDeviceCallback callback) override;

  void StartRouteTableTransaction(
      ::fidl::InterfaceRequest<fuchsia::netstack::RouteTableTransaction>
          routeTableTransaction,
      StartRouteTableTransactionCallback callback) override {}

  fidl::InterfaceRequestHandler<fuchsia::netstack::Netstack> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // Send a packet with UDP headers, including the ethernet and IPv6 headers.
  zx_status_t SendUdpPacket(void* packet, size_t length) const;

  // Send a raw packet.
  zx_status_t SendPacket(void* packet, size_t length) const;

  // Receive a raw packet.
  zx_status_t ReceivePacket(void* packet, size_t length, size_t* actual) const;

 private:
  fidl::BindingSet<fuchsia::netstack::Netstack> bindings_;
  fuchsia::hardware::ethernet::DeviceSyncPtr eth_device_;

  zx::fifo rx_;
  zx::fifo tx_;
  zx::vmo vmo_;
  uintptr_t io_addr_;
};

#endif  // GARNET_BIN_GUEST_INTEGRATION_MOCK_NETSTACK_H_
