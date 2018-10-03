// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/guest/device/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/trap.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/logging.h>
#include <trace-provider/provider.h>

#include "garnet/lib/machina/device/config.h"
#include "garnet/lib/machina/device/stream_base.h"

class VirtioConsoleImpl;

enum class Queue : uint16_t {
  RECEIVE = 0,
  TRANSMIT = 1,
};

// Stream for receive and transmit queues.
template <zx_signals_t Trigger,
          void (VirtioConsoleImpl::*M)(
              async_dispatcher_t* dispatcher, async::WaitBase* wait,
              zx_status_t status, const zx_packet_signal_t* signal)>
class ConsoleStream : public machina::StreamBase {
 public:
  ConsoleStream(VirtioConsoleImpl* impl) : wait_(impl) {}

  void Init(const zx::socket& socket, const machina::PhysMem& phys_mem,
            machina::VirtioQueue::InterruptFn interrupt) {
    wait_.set_object(socket.get());
    wait_.set_trigger(Trigger);
    StreamBase::Init(phys_mem, std::move(interrupt));
  }

  void WaitOnSocket(async_dispatcher_t* dispatcher) {
    zx_status_t status = wait_.Begin(dispatcher);
    FXL_CHECK(status == ZX_OK || status == ZX_ERR_ALREADY_EXISTS)
        << "Failed to wait on socket " << status;
  }

  template <typename F>
  void OnSocketReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                     F process_descriptor) {
    // If |process_descriptor| return ZX_ERR_SHOULD_WAIT, we may be in the
    // middle of processing a descriptor chain, therefore we should continue
    // where we left off.
    if (chain_.HasDescriptor()) {
      goto process;
    }
    for (; queue_.NextChain(&chain_); chain_.Return()) {
      while (chain_.NextDescriptor(&desc_)) {
      process:
        zx_status_t status = process_descriptor(&desc_);
        if (status == ZX_ERR_SHOULD_WAIT) {
          // If we have written to the descriptor chain, return it.
          if (*chain_.Used() > 0) {
            chain_.Return();
          }
          status = wait->Begin(dispatcher);
          FXL_CHECK(status == ZX_OK) << "Failed to wait on socket " << status;
          return;
        }
        FXL_CHECK(status == ZX_OK) << "Failed to operate on socket " << status;
      }
    }
  }

 private:
  async::WaitMethod<VirtioConsoleImpl, M> wait_;
};

// Implementation of a virtio-console device.
class VirtioConsoleImpl : public fuchsia::guest::device::VirtioConsole {
 public:
  VirtioConsoleImpl(component::StartupContext* context) {
    context->outgoing().AddPublicService(bindings_.GetHandler(this));
  }

 private:
  // |fuchsia::guest::device::VirtioConsole|
  void Start(fuchsia::guest::device::StartInfo start_info,
             zx::socket socket) override {
    FXL_CHECK(!event_) << "Device has already been started";

    event_ = std::move(start_info.event);
    zx_status_t status = phys_mem_.Init(std::move(start_info.vmo));
    FXL_CHECK(status == ZX_OK)
        << "Failed to init guest physical memory " << status;

    if (start_info.guest) {
      trap_addr_ = start_info.trap.addr;
      status = trap_.SetTrap(async_get_default_dispatcher(), start_info.guest,
                             start_info.trap.addr, start_info.trap.size);
      FXL_CHECK(status == ZX_OK) << "Failed to set trap " << status;
    }

    socket_ = std::move(socket);
    rx_stream_.Init(socket_, phys_mem_,
                    fit::bind_member(this, &VirtioConsoleImpl::Interrupt));
    tx_stream_.Init(socket_, phys_mem_,
                    fit::bind_member(this, &VirtioConsoleImpl::Interrupt));
  }

  // |fuchsia::guest::device::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                      zx_gpaddr_t avail, zx_gpaddr_t used) override {
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

  void NotifyQueue(uint16_t queue, async_dispatcher_t* dispatcher) {
    switch (static_cast<Queue>(queue)) {
      case Queue::RECEIVE:
        rx_stream_.WaitOnSocket(dispatcher);
        break;
      case Queue::TRANSMIT:
        tx_stream_.WaitOnSocket(dispatcher);
        break;
      default:
        FXL_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // |fuchsia::guest::device::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    NotifyQueue(queue, async_get_default_dispatcher());
  }

  // |fuchsia::guest::device::VirtioDevice|
  void Ready(uint32_t negotiated_features) override {}

  // Signals an interrupt for the device.
  zx_status_t Interrupt(uint8_t actions) {
    return event_.signal(0, static_cast<zx_signals_t>(actions)
                                << machina::kDeviceInterruptShift);
  }

  void OnQueueNotify(async_dispatcher_t* dispatcher,
                     async::GuestBellTrapBase* trap, zx_status_t status,
                     const zx_packet_guest_bell_t* bell) {
    FXL_CHECK(status == ZX_OK) << "Device trap failed " << status;
    uint16_t queue = machina::queue_from(trap_addr_, bell->addr);
    NotifyQueue(queue, dispatcher);
  }

  void OnSocketReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                        zx_status_t status, const zx_packet_signal_t* signal) {
    FXL_CHECK(status == ZX_OK) << "Wait for socket readable failed " << status;
    rx_stream_.OnSocketReady(dispatcher, wait, [this](auto desc) {
      FXL_CHECK(desc->writable) << "Descriptor is not writable";
      size_t actual = 0;
      zx_status_t status = socket_.read(0, desc->addr, desc->len, &actual);
      *rx_stream_.Used() += actual;
      return status;
    });
  }

  void OnSocketWritable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                        zx_status_t status, const zx_packet_signal_t* signal) {
    FXL_CHECK(status == ZX_OK) << "Wait for socket writable failed " << status;
    tx_stream_.OnSocketReady(dispatcher, wait, [this](auto desc) {
      FXL_CHECK(!desc->writable) << "Descriptor is not readable";
      size_t actual = 0;
      zx_status_t status = socket_.write(0, desc->addr, desc->len, &actual);
      // It's possible only part of the descriptor has been written to the
      // socket. If so we need to wait on ZX_SOCKET_WRITABLE again to write
      // the remainder of the payload.
      if (status == ZX_OK && desc->len > actual) {
        desc->addr = static_cast<uint8_t*>(desc->addr) + actual;
        desc->len -= actual;
        status = ZX_ERR_SHOULD_WAIT;
      }
      return status;
    });
  }

  fidl::BindingSet<VirtioConsole> bindings_;
  zx_gpaddr_t trap_addr_;
  zx::event event_;
  machina::PhysMem phys_mem_;
  zx::socket socket_;

  async::GuestBellTrapMethod<VirtioConsoleImpl,
                             &VirtioConsoleImpl::OnQueueNotify>
      trap_{this};
  ConsoleStream<ZX_SOCKET_READABLE, &VirtioConsoleImpl::OnSocketReadable>
      rx_stream_{this};
  ConsoleStream<ZX_SOCKET_WRITABLE, &VirtioConsoleImpl::OnSocketWritable>
      tx_stream_{this};
};

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  VirtioConsoleImpl virtio_console(context.get());
  return loop.Run();
}
