// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/interfaces/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/executor.h>
#include <lib/fit/defer.h>
#include <lib/fpromise/bridge.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/scope.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/trace-provider/provider.h>
#include <lib/zx/fifo.h>
#include <zircon/device/ethernet.h>
#include <zircon/status.h>

#include <memory>
#include <queue>

#include <virtio/net.h>

#include "guest_ethernet.h"
#include "src/connectivity/network/lib/net_interfaces/cpp/net_interfaces.h"
#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/stream_base.h"

static constexpr char kInterfaceName[] = "ethv0";

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
    for (; !packet_queue_.empty() && queue_.NextChain(&chain_); chain_.Return()) {
      Packet pkt = packet_queue_.front();
      chain_.NextDescriptor(&desc_);
      if (desc_.len < sizeof(virtio_net_hdr_t)) {
        FX_LOGS(ERROR) << "Malformed descriptor";
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
        FX_LOGS(ERROR) << "Dropping packet that's too large for the descriptor";
        continue;
      }
      memcpy(phys_mem_->ptr(offset, length), reinterpret_cast<void*>(pkt.addr), pkt.length);
      *chain_.Used() = static_cast<uint32_t>(pkt.length + sizeof(*header));
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

  bool ProcessDescriptor() {
    auto header = static_cast<virtio_net_hdr_t*>(desc_.addr);
    uintptr_t offset = phys_mem_->offset(header + 1);
    uintptr_t length = desc_.len - sizeof(*header);

    zx_status_t status =
        guest_ethernet_->Send(phys_mem_->ptr(offset, length), static_cast<uint16_t>(length));
    return status != ZX_ERR_SHOULD_WAIT;
  }

  void Notify() {
    // If Send returned ZX_ERR_SHOULD_WAIT last time Notify was called, then we should process that
    // descriptor first.
    if (chain_.IsValid()) {
      bool processed = ProcessDescriptor();
      if (!processed) {
        return;
      }
      chain_.Return();
    }

    for (; queue_.NextChain(&chain_); chain_.Return()) {
      chain_.NextDescriptor(&desc_);
      if (desc_.has_next) {
        // Section 5.1.6.2  Packet Transmission: The header and packet are added
        // as one output descriptor to the transmitq.
        if (!warned_) {
          warned_ = true;
          FX_LOGS(WARNING) << "Transmit packet and header must be on a single descriptor";
        }
        continue;
      }
      if (desc_.len < sizeof(virtio_net_hdr_t)) {
        FX_LOGS(ERROR) << "Failed to read descriptor header";
        continue;
      }

      bool processed = ProcessDescriptor();
      if (!processed) {
        // Stop processing and wait for GuestEthernet to notify us again. Do not return the
        // descriptor to the guest.
        return;
      }
    }
  }

 private:
  GuestEthernet* guest_ethernet_ = nullptr;
  const PhysMem* phys_mem_ = nullptr;
  bool warned_ = false;
};

class VirtioNetImpl : public DeviceBase<VirtioNetImpl>,
                      public fuchsia::virtualization::hardware::VirtioNet,
                      public GuestEthernetDevice {
 public:
  VirtioNetImpl(sys::ComponentContext* context) : DeviceBase(context), context_(*context) {
    netstack_ = context_.svc()->Connect<fuchsia::netstack::Netstack>();
    fuchsia::net::interfaces::StatePtr interfaces_state =
        context_.svc()->Connect<fuchsia::net::interfaces::State>();
    interfaces_state->GetWatcher(fuchsia::net::interfaces::WatcherOptions(), watcher_.NewRequest());
  }

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
  void Receive(uintptr_t addr, size_t length, const eth_fifo_entry_t& entry) override {
    rx_stream_.Receive(addr, length, entry);
  }

  // Called by GuestEthernet to notify us when the netstack is ready to receive packets.
  void ReadyToSend() override { tx_stream_.Notify(); }

  fuchsia::hardware::ethernet::MacAddress GetMacAddress() override { return mac_address_; }

 private:
  // |fuchsia::virtualization::hardware::VirtioNet|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info,
             fuchsia::hardware::ethernet::MacAddress mac_address, bool enable_bridge,
             StartCallback callback) override {
    PrepStart(std::move(start_info));
    rx_stream_.Init(&guest_ethernet_, phys_mem_,
                    fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioNetImpl::Interrupt));
    tx_stream_.Init(&guest_ethernet_, phys_mem_,
                    fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioNetImpl::Interrupt));

    mac_address_ = std::move(mac_address);

    fpromise::promise<uint32_t> guest_interface =
        CreateGuestInterface().and_then(fit::bind_member(this, &VirtioNetImpl::EnableInterface));

    if (enable_bridge) {
      fpromise::promise<uint32_t> bridge =
          fpromise::join_promises(FindHostInterface(), std::move(guest_interface))
              .and_then(fit::bind_member(this, &VirtioNetImpl::CreateBridgeInterface))
              .and_then(fit::bind_member(this, &VirtioNetImpl::EnableInterface));
      guest_interface = std::move(bridge);
    }

    executor_.schedule_task(
        guest_interface
            .and_then([callback = std::move(callback)](const uint32_t& nic_id) { callback(); })
            .or_else([] { FX_LOGS(FATAL) << "Failed to setup guest ethernet"; })
            .wrap_with(scope_));
  }

  fpromise::promise<uint32_t> CreateGuestInterface() {
    fpromise::bridge<uint32_t> bridge;

    fuchsia::netstack::InterfaceConfig config;
    config.name = kInterfaceName;
    auto callback = [completer = std::move(bridge.completer)](
                        fuchsia::netstack::Netstack_AddEthernetDevice_Result result) mutable {
      if (result.is_err()) {
        FX_LOGS(ERROR) << "Failed to create guest interface";
        completer.complete_error();
      } else {
        completer.complete_ok(result.response().nicid);
      }
    };
    netstack_->AddEthernetDevice("", std::move(config), device_binding_.NewBinding(),
                                 std::move(callback));

    return bridge.consumer.promise();
  }

  fpromise::promise<uint32_t> EnableInterface(const uint32_t& nic_id) {
    netstack_->SetInterfaceStatus(nic_id, true);
    return fpromise::make_ok_promise(nic_id);
  }

  void OnInterfacesEvent(fuchsia::net::interfaces::Event event,
                         fpromise::completer<uint32_t> completer) {
    std::optional result = [&event]() -> std::optional<fpromise::completer<uint32_t>::result_type> {
      switch (event.Which()) {
        case fuchsia::net::interfaces::Event::kExisting: {
          std::optional<net::interfaces::Properties> validated =
              net::interfaces::Properties::VerifyAndCreate(std::move(event.existing()));
          if (!validated.has_value()) {
            FX_LOGS(ERROR) << "malformed properties found in existing event from "
                              "fuchsia.net.interfaces/Watcher";
            return fpromise::error();
          }
          const net::interfaces::Properties& properties = validated.value();
          if (properties.IsGloballyRoutable()) {
            return fpromise::ok(static_cast<uint32_t>(properties.id()));
          }
          __FALLTHROUGH;
        }
        case fuchsia::net::interfaces::Event::kAdded:
        case fuchsia::net::interfaces::Event::kChanged:
        case fuchsia::net::interfaces::Event::kRemoved:
          return std::nullopt;
        case fuchsia::net::interfaces::Event::kIdle:
          FX_LOGS(ERROR) << "failed to find host interface";
          return fpromise::error();
        case fuchsia::net::interfaces::Event::Invalid:
          FX_LOGS(ERROR) << "invalid event received from fuchsia.net.interfaces/Watcher";
          return fpromise::error();
      }
    }();

    if (result.has_value()) {
      completer.complete_or_abandon(result.value());
    } else {
      watcher_->Watch(
          [this, completer = std::move(completer)](fuchsia::net::interfaces::Event event) mutable {
            OnInterfacesEvent(std::move(event), std::move(completer));
          });
    }
  }

  fpromise::promise<uint32_t> FindHostInterface() {
    fpromise::bridge<uint32_t> bridge;

    watcher_->Watch([this, completer = std::move(bridge.completer)](
                        fuchsia::net::interfaces::Event event) mutable {
      OnInterfacesEvent(std::move(event), std::move(completer));
    });

    return bridge.consumer.promise();
  }

  fpromise::promise<uint32_t> CreateBridgeInterface(
      const std::tuple<fpromise::result<uint32_t>, fpromise::result<uint32_t>>& nic_ids) {
    auto& [host_id, guest_id] = nic_ids;
    if (host_id.is_error() || guest_id.is_error()) {
      return fpromise::make_result_promise<uint32_t>(fpromise::error());
    }
    fpromise::bridge<uint32_t> bridge;

    auto callback = [completer = std::move(bridge.completer)](fuchsia::netstack::NetErr result,
                                                              uint32_t nic_id) mutable {
      if (result.status != fuchsia::netstack::Status::OK) {
        FX_LOGS(ERROR) << "Failed to create bridge interface: " << result.message;
        completer.complete_error();
      } else {
        completer.complete_ok(nic_id);
      }
    };
    netstack_->BridgeInterfaces({host_id.value(), guest_id.value()}, std::move(callback));

    return bridge.consumer.promise();
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

  sys::ComponentContext& context_;
  GuestEthernet guest_ethernet_{this};
  fidl::Binding<fuchsia::hardware::ethernet::Device> device_binding_ =
      fidl::Binding<fuchsia::hardware::ethernet::Device>(&guest_ethernet_);
  fuchsia::netstack::NetstackPtr netstack_;
  fuchsia::net::interfaces::WatcherPtr watcher_;

  RxStream rx_stream_;
  TxStream tx_stream_;

  uint32_t negotiated_features_;
  fpromise::scope scope_;
  async::Executor executor_ = async::Executor(async_get_default_dispatcher());

  fuchsia::hardware::ethernet::MacAddress mac_address_;
};

int main(int argc, char** argv) {
  syslog::SetTags({"virtio_net"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();

  VirtioNetImpl virtio_net(context.get());
  return loop.Run();
}
