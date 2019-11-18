// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_
#define SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_

#include <fuchsia/hardware/ethernet/c/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/bridge.h>
#include <zircon/device/ethernet.h>

#include <map>
#include <vector>

class Device {
 public:
  static zx_status_t Create(fuchsia::hardware::ethernet::DeviceSyncPtr eth_device,
                            std::unique_ptr<Device>* out);

  zx_status_t Start(async_dispatcher_t* dispatcher);

  fit::promise<std::vector<uint8_t>, zx_status_t> ReadPacket();
  fit::promise<void, zx_status_t> WritePacket(std::vector<uint8_t> packet);

 private:
  Device(fuchsia::hardware::ethernet::DeviceSyncPtr eth_device, zx::fifo rx, zx::fifo tx,
         zx::vmo vmo, uintptr_t io_addr)
      : eth_device_(std::move(eth_device)),
        rx_(std::move(rx)),
        tx_(std::move(tx)),
        vmo_(std::move(vmo)),
        io_addr_(io_addr) {
    rx_wait_.set_trigger(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED);
    rx_wait_.set_object(rx_.get());

    tx_wait_.set_trigger(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED);
    tx_wait_.set_object(tx_.get());
  }

  void OnReceive(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                 const zx_packet_signal_t* signal);
  void OnTransmit(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                  const zx_packet_signal_t* signal);

  fit::promise<eth_fifo_entry_t, zx_status_t> GetRxEntry();
  fit::promise<eth_fifo_entry_t, zx_status_t> GetTxEntry();

  fuchsia::hardware::ethernet::DeviceSyncPtr eth_device_;
  zx::fifo rx_;
  zx::fifo tx_;
  zx::vmo vmo_;
  const uintptr_t io_addr_;

  std::mutex mutex_;
  std::vector<fit::completer<eth_fifo_entry_t, zx_status_t>> rx_completers_ __TA_GUARDED(mutex_);
  std::vector<fit::completer<eth_fifo_entry_t, zx_status_t>> tx_completers_ __TA_GUARDED(mutex_);
  async::WaitMethod<Device, &Device::OnReceive> rx_wait_{this};
  async::WaitMethod<Device, &Device::OnTransmit> tx_wait_{this};
};

class FakeNetstack : public fuchsia::netstack::Netstack {
 public:
  FakeNetstack() {
    // Start a thread for the Device waiters. We can't use the main test thread, because it will
    // block to run test utils and deadlock the test.
    loop_.StartThread("FakeNetstack");
  }

  ~FakeNetstack() {
    loop_.Quit();
    loop_.JoinThreads();
    loop_.Shutdown();
  }

  void GetPortForService(std::string service, fuchsia::netstack::Protocol protocol,
                         GetPortForServiceCallback callback) override {}

  void GetAddress(std::string address, uint16_t port, GetAddressCallback callback) override {}

  void GetInterfaces(GetInterfacesCallback callback) override {}
  void GetInterfaces2(GetInterfaces2Callback callback) override {}

  void GetRouteTable(GetRouteTableCallback callback) override {}
  void GetRouteTable2(GetRouteTable2Callback callback) override {}

  void SetInterfaceStatus(uint32_t nicid, bool enabled) override {}

  void SetInterfaceAddress(uint32_t nicid, fuchsia::net::IpAddress addr, uint8_t prefixLen,
                           SetInterfaceAddressCallback callback) override;

  void RemoveInterfaceAddress(uint32_t nicid, fuchsia::net::IpAddress addr, uint8_t prefixLen,
                              RemoveInterfaceAddressCallback callback) override {}

  void SetInterfaceMetric(uint32_t nicid, uint32_t metric,
                          SetInterfaceMetricCallback callback) override {}

  void GetDhcpClient(uint32_t nicid, ::fidl::InterfaceRequest<::fuchsia::net::dhcp::Client> client,
                     GetDhcpClientCallback callback) override {}

  void BridgeInterfaces(std::vector<uint32_t> nicids, BridgeInterfacesCallback callback) override {}

  void AddEthernetDevice(std::string topological_path,
                         fuchsia::netstack::InterfaceConfig interfaceConfig,
                         ::fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device> device,
                         AddEthernetDeviceCallback callback) override;

  void StartRouteTableTransaction(
      ::fidl::InterfaceRequest<fuchsia::netstack::RouteTableTransaction> routeTableTransaction,
      StartRouteTableTransactionCallback callback) override {}

  fidl::InterfaceRequestHandler<fuchsia::netstack::Netstack> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // Send a packet with UDP headers, including the ethernet and IPv6 headers, to the interface with
  // the specified MAC address.
  fit::promise<void, zx_status_t> SendUdpPacket(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet);

  // Send a raw packet to the interface with the specified MAC address.
  fit::promise<void, zx_status_t> SendPacket(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet);

  // Receive a raw packet from the interface with the specified MAC address.
  fit::promise<std::vector<uint8_t>, zx_status_t> ReceivePacket(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr);

 private:
  struct CompareMacAddress {
    bool operator()(const fuchsia::hardware::ethernet::MacAddress& a,
                    const fuchsia::hardware::ethernet::MacAddress& b) const {
      // The octets of a MacAddress are a std::array, which implements lexigraphical ordering.
      return a.octets < b.octets;
    }
  };

  fit::promise<Device*> GetDevice(const fuchsia::hardware::ethernet::MacAddress& mac_addr);

  fidl::BindingSet<fuchsia::netstack::Netstack> bindings_;
  std::mutex mutex_;
  // Maps MAC addresses to devices.
  std::map<fuchsia::hardware::ethernet::MacAddress, std::unique_ptr<Device>, CompareMacAddress>
      devices_ __TA_GUARDED(mutex_);
  // Maps MAC addresses to completers, to enable the GetDevice promises.
  std::map<fuchsia::hardware::ethernet::MacAddress, std::vector<fit::completer<Device*>>,
           CompareMacAddress>
      completers_ __TA_GUARDED(mutex_);

  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

#endif  // SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_
