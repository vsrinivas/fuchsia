// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_
#define SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_

#include <fuchsia/net/interfaces/cpp/fidl_test_base.h>
#include <fuchsia/netstack/cpp/fidl_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fpromise/bridge.h>
#include <zircon/device/ethernet.h>

#include <algorithm>
#include <map>
#include <vector>

class Device {
 public:
  static zx_status_t Create(fuchsia::hardware::ethernet::DeviceSyncPtr eth_device,
                            std::unique_ptr<Device>* out);

  zx_status_t Start(async_dispatcher_t* dispatcher);

  fpromise::promise<std::vector<uint8_t>, zx_status_t> ReadPacket();
  fpromise::promise<void, zx_status_t> WritePacket(std::vector<uint8_t> packet);

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

  fpromise::promise<eth_fifo_entry_t, zx_status_t> GetRxEntry();
  fpromise::promise<eth_fifo_entry_t, zx_status_t> GetTxEntry();

  fuchsia::hardware::ethernet::DeviceSyncPtr eth_device_;
  zx::fifo rx_;
  zx::fifo tx_;
  zx::vmo vmo_;
  const uintptr_t io_addr_;

  std::mutex mutex_;
  std::vector<fpromise::completer<eth_fifo_entry_t, zx_status_t>> rx_completers_
      __TA_GUARDED(mutex_);
  std::vector<fpromise::completer<eth_fifo_entry_t, zx_status_t>> tx_completers_
      __TA_GUARDED(mutex_);
  async::WaitMethod<Device, &Device::OnReceive> rx_wait_{this};
  async::WaitMethod<Device, &Device::OnTransmit> tx_wait_{this};
};

class FakeState : public fuchsia::net::interfaces::testing::State_TestBase {
 public:
  fidl::InterfaceRequestHandler<fuchsia::net::interfaces::State> GetHandler() {
    return bindings_.GetHandler(this);
  }

 private:
  void NotImplemented_(const std::string& name) override;

  fidl::BindingSet<fuchsia::net::interfaces::State> bindings_;
};

class FakeNetstack : public fuchsia::netstack::testing::Netstack_TestBase {
 public:
  FakeNetstack() {
    // Start a thread for the Device waiters. We can't use the main test thread, because it will
    // block to run test utils and deadlock the test.
    loop_.StartThread("FakeNetstack");
  }

  // fuchsia::netstack::testing::Netstack_TestBase
  void BridgeInterfaces(std::vector<uint32_t> nicids, BridgeInterfacesCallback callback) override;
  void AddEthernetDevice(std::string topological_path,
                         fuchsia::netstack::InterfaceConfig interfaceConfig,
                         ::fidl::InterfaceHandle<::fuchsia::hardware::ethernet::Device> device,
                         AddEthernetDeviceCallback callback) override;

  // fuchsia::netstack::testing::Netstack_TestBase
  void SetInterfaceStatus(uint32_t nicid, bool enabled) override;

  fidl::InterfaceRequestHandler<fuchsia::netstack::Netstack> GetHandler() {
    return bindings_.GetHandler(this);
  }

  // Send a packet with UDP headers, including the ethernet and IPv6 headers, to the interface with
  // the specified MAC address.
  fpromise::promise<void, zx_status_t> SendUdpPacket(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet);

  // Send a raw packet to the interface with the specified MAC address.
  fpromise::promise<void, zx_status_t> SendPacket(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr, std::vector<uint8_t> packet);

  // Receive a raw packet from the interface with the specified MAC address.
  fpromise::promise<std::vector<uint8_t>, zx_status_t> ReceivePacket(
      const fuchsia::hardware::ethernet::MacAddress& mac_addr);

 private:
  struct CompareMacAddress {
    bool operator()(const fuchsia::hardware::ethernet::MacAddress& a,
                    const fuchsia::hardware::ethernet::MacAddress& b) const {
      return std::lexicographical_compare(a.octets.begin(), a.octets.end(), b.octets.begin(),
                                          b.octets.end());
    }
  };

  fpromise::promise<Device*> GetDevice(const fuchsia::hardware::ethernet::MacAddress& mac_addr);

  void NotImplemented_(const std::string& name) override;

  fidl::BindingSet<fuchsia::netstack::Netstack> bindings_;
  std::mutex mutex_;
  // Maps MAC addresses to devices.
  std::map<fuchsia::hardware::ethernet::MacAddress, std::unique_ptr<Device>, CompareMacAddress>
      devices_ __TA_GUARDED(mutex_);
  // Maps MAC addresses to completers, to enable the GetDevice promises.
  std::map<fuchsia::hardware::ethernet::MacAddress, std::vector<fpromise::completer<Device*>>,
           CompareMacAddress>
      completers_ __TA_GUARDED(mutex_);

  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};

  uint8_t nic_counter_ = 1;
};

#endif  // SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_H_
