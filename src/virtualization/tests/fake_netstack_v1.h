// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_V1_H_
#define SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_V1_H_

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
#include <queue>
#include <vector>

// TODO(fxbug.dev/87034): Remove this implementation once all devices
// have migrated to the new fuchsia.net.stack FIDL protocol.

namespace fake_netstack::v1 {

class Device {
 public:
  static zx_status_t Create(async_dispatcher_t* dispatcher,
                            fuchsia::hardware::ethernet::DeviceSyncPtr eth_device,
                            std::unique_ptr<Device>* out);

  zx_status_t Start();

  fpromise::promise<std::vector<uint8_t>, zx_status_t> ReadPacket();
  fpromise::promise<void, zx_status_t> WritePacket(std::vector<uint8_t> packet);

 private:
  Device(async_dispatcher_t* dispatcher, fuchsia::hardware::ethernet::DeviceSyncPtr eth_device,
         zx::fifo rx, std::vector<eth_fifo_entry_t> rx_entries, zx::fifo tx,
         std::vector<eth_fifo_entry_t> tx_entries, zx::vmo vmo, uint8_t* io_addr)
      : dispatcher_(dispatcher),
        eth_device_(std::move(eth_device)),
        rx_(std::move(rx), std::move(rx_entries), FIFO::Direction::Outbound),
        tx_(std::move(tx), std::move(tx_entries), FIFO::Direction::Inbound),
        vmo_(std::move(vmo)),
        io_addr_(io_addr) {}

  class FIFO {
   public:
    enum class Direction {
      Inbound,
      Outbound,
    };
    FIFO(zx::fifo fifo, std::vector<eth_fifo_entry_t> entries, Direction direction)
        : depth_(entries.size()), fifo_(std::move(fifo)) {
      switch (direction) {
        case Direction::Inbound:
          inbound_entries_ = std::move(entries);
          break;
        case Direction::Outbound:
          outbound_entries_ = std::move(entries);
          break;
      }
      inbound_wait_.set_trigger(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED);
      inbound_wait_.set_object(fifo_.get());
      outbound_wait_.set_trigger(ZX_FIFO_WRITABLE | ZX_FIFO_PEER_CLOSED);
      outbound_wait_.set_object(fifo_.get());
    }

    void InboundHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal);

    void OutboundHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                         const zx_packet_signal_t* signal);

    fpromise::promise<eth_fifo_entry_t, zx_status_t> GetEntry();

    const size_t depth_;
    zx::fifo fifo_;
    std::mutex mutex_;
    std::vector<eth_fifo_entry_t> inbound_entries_ __TA_GUARDED(mutex_);
    std::vector<eth_fifo_entry_t> outbound_entries_ __TA_GUARDED(mutex_);
    std::queue<fpromise::completer<eth_fifo_entry_t, zx_status_t>> completers_ __TA_GUARDED(mutex_);
    async::WaitMethod<FIFO, &FIFO::InboundHandler> inbound_wait_{this};
    async::WaitMethod<FIFO, &FIFO::OutboundHandler> outbound_wait_{this};
  };

  async_dispatcher_t* const dispatcher_;

  fuchsia::hardware::ethernet::DeviceSyncPtr eth_device_;

  FIFO rx_, tx_;

  zx::vmo vmo_;
  uint8_t* const io_addr_;
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

  fpromise::promise<Device*> GetDevice(const fuchsia::hardware::ethernet::MacAddress& mac_addr);

 private:
  struct CompareMacAddress {
    bool operator()(const fuchsia::hardware::ethernet::MacAddress& a,
                    const fuchsia::hardware::ethernet::MacAddress& b) const {
      return std::lexicographical_compare(a.octets.begin(), a.octets.end(), b.octets.begin(),
                                          b.octets.end());
    }
  };

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

}  // namespace fake_netstack::v1

#endif  // SRC_VIRTUALIZATION_TESTS_FAKE_NETSTACK_V1_H_
