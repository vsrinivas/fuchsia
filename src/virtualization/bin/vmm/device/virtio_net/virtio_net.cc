// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/cpp/fidl.h>
#include <fuchsia/net/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/defer.h>
#include <lib/fit/thread_checker.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/trace-provider/provider.h>
#include <zircon/status.h>

#include <memory>
#include <queue>

#include <virtio/net.h>

#include "guest_ethernet.h"
#include "src/connectivity/network/lib/net_interfaces/cpp/net_interfaces.h"
#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/virtio_queue.h"

enum class Queue : uint16_t {
  RECEIVE = 0,
  TRANSMIT = 1,
};

class RxStream {
 public:
  void Init(GuestEthernet* guest_ethernet, const PhysMem& phys_mem,
            VirtioQueue::InterruptFn interrupt) {
    std::lock_guard guard(checker_);
    guest_ethernet_ = guest_ethernet;
    phys_mem_ = &phys_mem;
    queue_.set_phys_mem(&phys_mem);
    queue_.set_interrupt(std::move(interrupt));
  }

  void Configure(uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail, zx_gpaddr_t used) {
    std::lock_guard guard(checker_);
    queue_.Configure(size, desc, avail, used);
  }

  void Notify() {
    std::lock_guard guard(checker_);
    for (VirtioChain chain; !packet_queue_.empty() && queue_.NextChain(&chain); chain.Return()) {
      Packet pkt = packet_queue_.front();
      VirtioDescriptor desc;
      chain.NextDescriptor(&desc);
      if (desc.len < sizeof(virtio_net_hdr_t)) {
        FX_LOGS(ERROR) << "Malformed descriptor";
        continue;
      }
      auto header = static_cast<virtio_net_hdr_t*>(desc.addr);
      // Section 5.1.6.4.1 Device Requirements: Processing of Incoming Packets

      // If VIRTIO_NET_F_MRG_RXBUF has not been negotiated, the device MUST
      // set num_buffers to 1.
      header->num_buffers = 1;

      // If none of the VIRTIO_NET_F_GUEST_TSO4, TSO6 or UFO options have been
      // negotiated, the device MUST set gso_type to VIRTIO_NET_HDR_GSO_NONE.
      header->base.gso_type = VIRTIO_NET_HDR_GSO_NONE;

      // If VIRTIO_NET_F_GUEST_CSUM is not negotiated, the device MUST set
      // flags to zero and SHOULD supply a fully checksummed packet to the
      // driver.
      header->base.flags = 0;

      uintptr_t offset = phys_mem_->offset(header + 1);
      uintptr_t length = desc.len - sizeof(*header);
      packet_queue_.pop();
      if (length < pkt.data.size()) {
        // 5.1.6.3.1 Driver Requirements: Setting Up Receive Buffers: the driver
        // SHOULD populate the receive queue(s) with buffers of at least 1526
        // bytes.

        // If the descriptor is too small for the packet then the driver is
        // misbehaving (our MTU is 1500).
        FX_LOGS(ERROR) << "Dropping packet that's too large for the descriptor";
        continue;
      }
      memcpy(phys_mem_->ptr(offset, length), pkt.data.data(), pkt.data.size());
      *chain.Used() = static_cast<uint32_t>(pkt.data.size() + sizeof(*header));
      guest_ethernet_->Complete(pkt.id, ZX_OK);
    }
  }

  void Receive(cpp20::span<const uint8_t> data, uint32_t id) {
    std::lock_guard guard(checker_);
    packet_queue_.push(Packet{data, id});
    Notify();
  }

 private:
  struct Packet {
    cpp20::span<const uint8_t> data;
    uint32_t id;
  };

  fit::thread_checker checker_;
  GuestEthernet* guest_ethernet_ = nullptr;
  const PhysMem* phys_mem_ = nullptr;
  std::queue<Packet> packet_queue_ __TA_GUARDED(checker_);
  VirtioQueue queue_;
};

class TxStream {
 public:
  ~TxStream() {
    // Drop any pending chain.
    if (pending_chain_.IsValid()) {
      pending_chain_.Return();
    }
  }

  void Configure(uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail, zx_gpaddr_t used) {
    std::lock_guard guard(checker_);
    queue_.Configure(size, desc, avail, used);
  }

  void Init(GuestEthernet* guest_ethernet, const PhysMem& phys_mem,
            VirtioQueue::InterruptFn interrupt) {
    std::lock_guard guard(checker_);
    guest_ethernet_ = guest_ethernet;
    phys_mem_ = &phys_mem;
    queue_.set_phys_mem(&phys_mem);
    queue_.set_interrupt(std::move(interrupt));
  }

  void Notify() {
    std::lock_guard guard(checker_);

    // If Send returned ZX_ERR_SHOULD_WAIT last time Notify was called, then we should process that
    // descriptor first.
    if (pending_chain_.IsValid()) {
      bool processed = ProcessDescriptor(pending_desc_);
      if (!processed) {
        return;
      }
      pending_chain_.Return();
    }

    for (VirtioChain chain; queue_.NextChain(&chain); chain.Return()) {
      VirtioDescriptor desc;
      chain.NextDescriptor(&desc);
      if (desc.has_next) {
        // Section 5.1.6.2  Packet Transmission: The header and packet are added
        // as one output descriptor to the transmitq.
        FX_LOGS_FIRST_N(WARNING, 1) << "Transmit packet and header must be on a single descriptor";
        continue;
      }
      if (desc.len < sizeof(virtio_net_hdr_t)) {
        FX_LOGS(ERROR) << "Failed to read descriptor header";
        continue;
      }

      bool processed = ProcessDescriptor(desc);
      if (!processed) {
        // Stop processing and wait for GuestEthernet to notify us again. Do not return the
        // descriptor to the guest.
        FX_DCHECK(!pending_chain_.IsValid());
        pending_desc_ = desc;
        pending_chain_ = std::move(chain);
        return;
      }
    }
  }

 private:
  bool ProcessDescriptor(VirtioDescriptor& desc) __TA_REQUIRES(checker_) {
    auto header = static_cast<virtio_net_hdr_t*>(desc.addr);
    uintptr_t offset = phys_mem_->offset(header + 1);
    uintptr_t length = desc.len - sizeof(*header);

    zx_status_t status =
        guest_ethernet_->Send(phys_mem_->ptr(offset, length), static_cast<uint16_t>(length));
    return status != ZX_ERR_SHOULD_WAIT;
  }

  fit::thread_checker checker_;
  GuestEthernet* guest_ethernet_ = nullptr;
  const PhysMem* phys_mem_ = nullptr;
  VirtioQueue queue_;

  // Pending chain and descriptor.
  //
  // Tracks a chain that was read from the guest but was unable to be processed
  // immediately.
  VirtioDescriptor pending_desc_ __TA_GUARDED(checker_);
  VirtioChain pending_chain_ __TA_GUARDED(checker_);
};

class VirtioNetImpl : public DeviceBase<VirtioNetImpl>,
                      public fuchsia::virtualization::hardware::VirtioNet,
                      public GuestEthernetDevice {
 public:
  explicit VirtioNetImpl(async_dispatcher_t* dispatcher, sys::ComponentContext* context)
      : DeviceBase(context),
        dispatcher_(dispatcher),
        context_(*context),
        guest_ethernet_(dispatcher, this) {}

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    switch (static_cast<Queue>(queue)) {
      case Queue::RECEIVE:
        rx_stream_.Notify();
        break;
      case Queue::TRANSMIT:
        tx_stream_.Notify();
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // Called by GuestEthernet to notify us when the netstack is trying to send a packet to the guest.
  void Receive(cpp20::span<const uint8_t> data, uint32_t id) override {
    rx_stream_.Receive(data, id);
  }

  // Called by GuestEthernet to notify us when the netstack is ready to receive packets.
  void ReadyToSend() override { tx_stream_.Notify(); }

  fuchsia::hardware::ethernet::MacAddress GetMacAddress() override { return mac_address_; }

 private:
  // |fuchsia::virtualization::hardware::VirtioNet|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info,
             fuchsia::hardware::ethernet::MacAddress mac_address, bool enable_bridge,
             StartCallback callback) override {
    // Set up VMM-related resources.
    PrepStart(std::move(start_info));
    rx_stream_.Init(&guest_ethernet_, phys_mem_,
                    fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioNetImpl::Interrupt));
    tx_stream_.Init(&guest_ethernet_, phys_mem_,
                    fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioNetImpl::Interrupt));

    mac_address_ = mac_address;

    // Connect to netstack, and create the ethernet interface
    zx_status_t status = CreateGuestInterface();
    if (status != ZX_OK) {
      bindings_.CloseAll(status);
      return;
    }

    callback();
  }

  // Create a GuestEthernet interface and connect it to Netstack.
  zx_status_t CreateGuestInterface() {
    // Connect to netstack.
    netstack_.set_error_handler([](zx_status_t status) {
      FX_PLOGS(WARNING, status) << "Connection to Netstack unexpectedly closed";
    });
    zx_status_t status = context_.svc()->Connect<fuchsia::net::virtualization::Control>(
        netstack_.NewRequest(dispatcher_));
    if (status != ZX_OK) {
      FX_PLOGS(WARNING, status) << "Failed to connect to netstack";
      return status;
    }

    // Set up the GuestEthernet device.
    zx::result<std::unique_ptr<network::NetworkDeviceInterface>> device_interface =
        network::NetworkDeviceInterface::Create(dispatcher_,
                                                guest_ethernet_.GetNetworkDeviceImplClient());
    if (device_interface.is_error()) {
      FX_PLOGS(WARNING, status) << "Failed to create guest interface";
      return status;
    }
    device_interface_ = std::move(device_interface.value());

    // Create a connection to the device.
    fidl::ClientEnd<fuchsia_hardware_network::Port> port;
    status = device_interface_->BindPort(
        GuestEthernet::kPortId,
        fidl::ServerEnd<fuchsia_hardware_network::Port>(fidl::CreateEndpoints(&port).value()));
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Internal error: could not bind to GuestEthernet server";
      return status;
    }

    // Create a new network group.
    fuchsia::net::virtualization::Config config;
    config.set_bridged(fuchsia::net::virtualization::Bridged{});
    netstack_->CreateNetwork(std::move(config), network_.NewRequest());

    // Add our GuestEthernet device to the network.
    interface_registration_.set_error_handler(
        [](zx_status_t status) { FX_PLOGS(WARNING, status) << "Connection to Netstack closed"; });
    network_->AddPort(fidl::InterfaceHandle<fuchsia::hardware::network::Port>(port.TakeChannel()),
                      interface_registration_.NewRequest());

    return ZX_OK;
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                      zx_gpaddr_t used, ConfigureQueueCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    switch (static_cast<Queue>(queue)) {
      case Queue::RECEIVE:
        rx_stream_.Configure(size, desc, avail, used);
        break;
      case Queue::TRANSMIT:
        tx_stream_.Configure(size, desc, avail, used);
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void Ready(uint32_t negotiated_features, ReadyCallback callback) override {
    negotiated_features_ = negotiated_features;
    callback();
  }

  async_dispatcher_t* dispatcher_;  // Owned elsewhere.
  sys::ComponentContext& context_;
  GuestEthernet guest_ethernet_;
  std::unique_ptr<network::NetworkDeviceInterface> device_interface_;
  fuchsia::net::virtualization::ControlPtr netstack_;
  fuchsia::net::virtualization::NetworkPtr network_;
  fuchsia::net::virtualization::InterfacePtr interface_registration_;

  RxStream rx_stream_;
  TxStream tx_stream_;

  uint32_t negotiated_features_;

  fuchsia::hardware::ethernet::MacAddress mac_address_;
};

int main(int argc, char** argv) {
  syslog::SetTags({"virtio_net"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  VirtioNetImpl virtio_net(loop.dispatcher(), context.get());
  return loop.Run();
}
