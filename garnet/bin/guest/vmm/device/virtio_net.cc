// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <queue>

#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async_promise/executor.h>
#include <lib/fit/bridge.h>
#include <lib/fit/defer.h>
#include <lib/fit/promise.h>
#include <lib/fit/scope.h>
#include <lib/zx/fifo.h>
#include <trace-provider/provider.h>
#include <virtio/net.h>
#include <zircon/device/ethernet.h>

#include "garnet/bin/guest/vmm/device/device_base.h"
#include "garnet/bin/guest/vmm/device/stream_base.h"

#include "guest_ethernet.h"

static constexpr char kInterfacePath[] = "/dev/class/ethernet/virtio";
static constexpr char kInterfaceName[] = "ethv0";
static constexpr uint8_t kIpv4Address[4] = {10, 0, 0, 1};
static constexpr uint8_t kPrefixLength = 24;

enum class Queue : uint16_t {
  RECEIVE = 0,
  TRANSMIT = 1,
};

class RxStream : public StreamBase {
 public:
  void Init(GuestEthernet* guest_ethernet, const PhysMem& phys_mem,
            VirtioQueue::InterruptFn interrupt) {
    guest_ethernet_ = guest_ethernet;
    phys_mem_ = &phys_mem;
    StreamBase::Init(phys_mem, std::move(interrupt));
  }

  void Notify() {
    for (; !packet_queue_.empty() && queue_.NextChain(&chain_);
         chain_.Return()) {
      Packet pkt = packet_queue_.front();
      chain_.NextDescriptor(&desc_);
      if (desc_.len < sizeof(virtio_net_hdr_t)) {
        FXL_LOG(ERROR) << "Malformed descriptor";
        continue;
      }
      auto header = static_cast<virtio_net_hdr_t*>(desc_.addr);
      // Section 5.1.6.4.1 Device Requirements: Processing of Incoming Packets

      // If VIRTIO_NET_F_MRG_RXBUF has not been negotiated, the device MUST
      // set num_buffers to 1.
      header->num_buffers = 1;

      // If none of the VIRTIO_NET_F_GUEST_TSO4, TSO6 or UFO options have been
      // negotiated, the device MUST set gso_type to VIRTIO_NET_HDR_GSO_NONE.
      header->gso_type = VIRTIO_NET_HDR_GSO_NONE;

      // If VIRTIO_NET_F_GUEST_CSUM is not negotiated, the device MUST set
      // flags to zero and SHOULD supply a fully checksummed packet to the
      // driver.
      header->flags = 0;

      uintptr_t offset = phys_mem_->offset(header + 1);
      uintptr_t length = desc_.len - sizeof(*header);
      packet_queue_.pop();
      if (length < pkt.length) {
        // 5.1.6.3.1 Driver Requirements: Setting Up Receive Buffers: the driver
        // SHOULD populate the receive queue(s) with buffers of at least 1526
        // bytes.

        // If the descriptor is too small for the packet then the driver is
        // misbehaving (our MTU is 1500).
        FXL_LOG(ERROR) << "Dropping packet that's too large for the descriptor";
        continue;
      }
      memcpy(phys_mem_->as<void>(offset, length),
             reinterpret_cast<void*>(pkt.addr), pkt.length);
      *chain_.Used() = pkt.length + sizeof(*header);
      pkt.entry.flags = ETH_FIFO_TX_OK;
      guest_ethernet_->Complete(pkt.entry);
    }
  }

  void Receive(uintptr_t addr, size_t length, const eth_fifo_entry_t& entry) {
    packet_queue_.push(Packet{addr, length, entry});
    Notify();
  }

 private:
  struct Packet {
    uintptr_t addr;
    size_t length;
    eth_fifo_entry_t entry;
  };

  GuestEthernet* guest_ethernet_ = nullptr;
  const PhysMem* phys_mem_ = nullptr;
  std::queue<Packet> packet_queue_;
};

class TxStream : public StreamBase {
 public:
  void Init(GuestEthernet* guest_ethernet, const PhysMem& phys_mem,
            VirtioQueue::InterruptFn interrupt) {
    guest_ethernet_ = guest_ethernet;
    phys_mem_ = &phys_mem;
    StreamBase::Init(phys_mem, std::move(interrupt));
  }

  void Notify() {
    for (; queue_.NextChain(&chain_); chain_.Return()) {
      chain_.NextDescriptor(&desc_);
      if (desc_.has_next) {
        // Section 5.1.6.2  Packet Transmission: The header and packet are added
        // as one output descriptor to the transmitq.
        static bool warned = false;
        if (!warned) {
          warned = true;
          FXL_LOG(ERROR)
              << "Transmit packet and header must be on a single descriptor";
        }
        continue;
      }
      if (desc_.len < sizeof(virtio_net_hdr_t)) {
        FXL_LOG(ERROR) << "Failed to read descriptor header";
        continue;
      }
      auto header = static_cast<virtio_net_hdr_t*>(desc_.addr);
      uintptr_t offset = phys_mem_->offset(header + 1);
      uintptr_t length = desc_.len - sizeof(*header);
      guest_ethernet_->Send(phys_mem_->as<void>(offset, length), length);
    }
  }

 private:
  GuestEthernet* guest_ethernet_ = nullptr;
  const PhysMem* phys_mem_ = nullptr;
};

class VirtioNetImpl : public DeviceBase<VirtioNetImpl>,
                      public fuchsia::guest::device::VirtioNet,
                      public GuestEthernetReceiver {
 public:
  VirtioNetImpl(component::StartupContext* context)
      : DeviceBase(context), context_(*context) {
    netstack_ =
        context_.ConnectToEnvironmentService<fuchsia::netstack::Netstack>();
  }

  // |fuchsia::guest::device::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    switch (static_cast<Queue>(queue)) {
      case Queue::RECEIVE:
        rx_stream_.Notify();
        break;
      case Queue::TRANSMIT:
        tx_stream_.Notify();
        break;
      default:
        FXL_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // Called by GuestEthernet to notify us when the netstack is trying to send a
  // packet to the guest.
  void Receive(uintptr_t addr, size_t length,
               const eth_fifo_entry_t& entry) override {
    rx_stream_.Receive(addr, length, entry);
  }

 private:
  // |fuchsia::guest::device::VirtioNet|
  void Start(fuchsia::guest::device::StartInfo start_info,
             StartCallback callback) override {
    PrepStart(std::move(start_info));

    fuchsia::net::Ipv4Address ipv4;
    memcpy(ipv4.addr.data(), kIpv4Address, 4);

    fuchsia::net::IpAddress addr;
    addr.set_ipv4(ipv4);

    fuchsia::net::Subnet subnet;
    subnet.addr.set_ipv4(ipv4);
    subnet.prefix_len = kPrefixLength;

    fuchsia::netstack::InterfaceConfig config;
    config.name = kInterfaceName;
    config.ip_address_config.set_static_ip(std::move(subnet));

    executor_.schedule_task(
        AddEthernetDevice(kInterfacePath, std::move(config))
            .and_then([this, addr = std::move(addr)](const uint32_t& nic_id) mutable {
              return SetInterfaceAddress(nic_id, std::move(addr));
            })
            .and_then([this](const uint32_t& nic_id) mutable {
              netstack_->SetInterfaceStatus(nic_id, true);
            })
            .or_else([]() mutable {
              FXL_CHECK(false) << "Failed to set ethernet IP address.";
            })
            .and_then([this]() {
              rx_stream_.Init(&guest_ethernet_, phys_mem_,
                              fit::bind_member<zx_status_t, DeviceBase>(
                                  this, &VirtioNetImpl::Interrupt));
              tx_stream_.Init(&guest_ethernet_, phys_mem_,
                              fit::bind_member<zx_status_t, DeviceBase>(
                                  this, &VirtioNetImpl::Interrupt));
            })
            .and_then(std::move(callback))
            .wrap_with(scope_));
  }

  fit::promise<uint32_t> AddEthernetDevice(
      const char* interface_path, fuchsia::netstack::InterfaceConfig config) {
    fit::bridge<uint32_t> bridge;

    netstack_->AddEthernetDevice(
        interface_path, std::move(config), device_binding_.NewBinding(),
        [completer = std::move(bridge.completer)](uint32_t id) mutable {
          completer.complete_ok(id);
        });

    return bridge.consumer.promise();
  }

  fit::promise<uint32_t> SetInterfaceAddress(uint32_t nic_id,
                                             fuchsia::net::IpAddress addr) {
    fit::bridge<uint32_t> bridge;

    netstack_->SetInterfaceAddress(
        nic_id, std::move(addr), kPrefixLength,
        [nic_id, completer = std::move(bridge.completer)](
            fuchsia::netstack::NetErr err) mutable {
          if (err.status == fuchsia::netstack::Status::OK) {
            completer.complete_ok(nic_id);
          } else {
            FXL_LOG(ERROR) << "Failed to set interface address with "
                           << static_cast<uint32_t>(err.status) << " "
                           << err.message;
            completer.complete_error();
          }
        });
    return bridge.consumer.promise();
  }

  // |fuchsia::guest::device::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                      zx_gpaddr_t avail, zx_gpaddr_t used,
                      ConfigureQueueCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    switch (static_cast<Queue>(queue)) {
      case Queue::RECEIVE:
        rx_stream_.Configure(size, desc, avail, used);
        break;
      case Queue::TRANSMIT:
        tx_stream_.Configure(size, desc, avail, used);
        break;
      default:
        FXL_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // |fuchsia::guest::device::VirtioDevice|
  void Ready(uint32_t negotiated_features, ReadyCallback callback) override {
    negotiated_features_ = negotiated_features;
    callback();
  }

  component::StartupContext& context_;
  GuestEthernet guest_ethernet_{this};
  fidl::Binding<fuchsia::hardware::ethernet::Device> device_binding_ =
      fidl::Binding<fuchsia::hardware::ethernet::Device>(&guest_ethernet_);
  fuchsia::netstack::NetstackPtr netstack_;

  RxStream rx_stream_;
  TxStream tx_stream_;

  uint32_t negotiated_features_;
  fit::scope scope_;
  async::Executor executor_ = async::Executor(async_get_default_dispatcher());
};

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  VirtioNetImpl virtio_net(context.get());

  return loop.Run();
}
