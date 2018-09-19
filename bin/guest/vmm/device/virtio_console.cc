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
#include "garnet/lib/machina/device/virtio_queue.h"

class VirtioConsoleImpl;

// Base class to abstracts out the queue handling logic for each stream.
struct StreamBase {
  machina::VirtioQueue queue;
  machina::VirtioDescriptor desc;
  uint16_t head;
  uint32_t used;

  StreamBase() { Reset(); }

  // Fetches the next descriptor in a chain, otherwise returns false.
  bool NextDescriptor() {
    if (!desc.has_next) {
      return false;
    }
    if (desc.addr == nullptr) {
      zx_status_t status = queue.NextAvail(&head);
      FXL_CHECK(status == ZX_OK)
          << "Failed to find an available descriptor " << status;
      desc.next = head;
    }
    zx_status_t status = queue.ReadDesc(desc.next, &desc);
    FXL_CHECK(status == ZX_OK) << "Failed to read descriptor " << status;
    return true;
  }

  // Returns the descriptor chain back to the queue.
  void ReturnChain() {
    zx_status_t status = queue.Return(head, used);
    FXL_CHECK(status == ZX_OK)
        << "Failed to return descriptor to queue " << status;
    Reset();
  }

  // Returns whether we have a descriptor chain available to process.
  bool HasChain() { return queue.HasAvail(); }
  // Returns whether we are in the middle of processing a descriptor.
  bool HasDescriptor() { return desc.addr != nullptr; }

 private:
  void Reset() {
    desc.addr = nullptr;
    desc.has_next = true;
    used = 0;
  }
};

// Concrete class for managing a stream.
template <zx_signals_t Trigger,
          void (VirtioConsoleImpl::*F)(
              async_dispatcher_t* dispatcher, async::WaitBase* wait,
              zx_status_t status, const zx_packet_signal_t* signal)>
struct Stream : public StreamBase {
  async::WaitMethod<VirtioConsoleImpl, F> wait;

  Stream(VirtioConsoleImpl* impl) : wait(impl) {}

  void Init(const zx::socket& socket, const machina::PhysMem& phys_mem,
            machina::VirtioQueue::InterruptFn interrupt) {
    wait.set_object(socket.get());
    wait.set_trigger(Trigger);
    queue.set_phys_mem(&phys_mem);
    queue.set_interrupt(std::move(interrupt));
  }
};

// Implementation of a virtio-console device.
class VirtioConsoleImpl : public fuchsia::guest::device::VirtioConsole {
 public:
  VirtioConsoleImpl(component::StartupContext* context) {
    context->outgoing().AddPublicService(bindings_.GetHandler(this));
  }

 private:
  std::pair<StreamBase*, async::WaitBase*> StreamForQueue(uint16_t queue) {
    switch (queue) {
      case /* RX queue */ 0:
        return std::make_pair(&rx_stream_, &rx_stream_.wait);
      case /* TX queue */ 1:
        return std::make_pair(&tx_stream_, &tx_stream_.wait);
      default:
        FXL_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // |fuchsia::guest::device::VirtioConsole|
  void Start(fuchsia::guest::device::StartInfo start_info,
             zx::socket socket) override {
    FXL_CHECK(!socket_) << "Device has already been started";

    trap_addr_ = start_info.trap.addr;
    event_ = std::move(start_info.event);
    zx_status_t status = phys_mem_.Init(std::move(start_info.vmo));
    FXL_CHECK(status == ZX_OK)
        << "Failed to init guest physical memory " << status;

    socket_ = std::move(socket);
    tx_stream_.Init(socket_, phys_mem_,
                    fit::bind_member(this, &VirtioConsoleImpl::Interrupt));
    rx_stream_.Init(socket_, phys_mem_,
                    fit::bind_member(this, &VirtioConsoleImpl::Interrupt));

    status = trap_.SetTrap(async_get_default_dispatcher(), start_info.guest,
                           start_info.trap.addr, start_info.trap.size);
    FXL_CHECK(status == ZX_OK) << "Failed to set trap " << status;
  }

  // |fuchsia::guest::device::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                      zx_gpaddr_t avail, zx_gpaddr_t used) override {
    StreamBase* stream;
    std::tie(stream, std::ignore) = StreamForQueue(queue);
    stream->queue.Configure(size, desc, avail, used);
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
    const uint16_t queue =
        (bell->addr - trap_addr_) / machina::kQueueNotifyMultiplier;
    StreamBase* stream;
    async::WaitBase* wait;
    std::tie(stream, wait) = StreamForQueue(queue);
    status = wait->Begin(dispatcher);
    FXL_CHECK(status == ZX_OK || status == ZX_ERR_ALREADY_EXISTS)
        << "Failed to wait on socket " << status;
  }

  void OnSocketWritable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                        zx_status_t status, const zx_packet_signal_t* signal) {
    FXL_CHECK(status == ZX_OK) << "Wait for socket writable failed " << status;
    OnSocketReady(dispatcher, wait, &tx_stream_, [this] {
      machina::VirtioDescriptor* desc = &tx_stream_.desc;
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

  void OnSocketReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                        zx_status_t status, const zx_packet_signal_t* signal) {
    FXL_CHECK(status == ZX_OK) << "Wait for socket readable failed " << status;
    OnSocketReady(dispatcher, wait, &rx_stream_, [this] {
      machina::VirtioDescriptor* desc = &rx_stream_.desc;
      FXL_CHECK(desc->writable) << "Descriptor is not writable";
      size_t actual = 0;
      zx_status_t status = socket_.read(0, desc->addr, desc->len, &actual);
      rx_stream_.used += actual;
      return status;
    });
  }

  template <typename F>
  void OnSocketReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                     StreamBase* stream, F process_descriptor) {
    if (stream->HasDescriptor()) {
      goto process;
    }
    for (; stream->HasChain(); stream->ReturnChain()) {
      while (stream->NextDescriptor()) {
      process:
        zx_status_t status = process_descriptor();
        if (status == ZX_ERR_SHOULD_WAIT) {
          // If we have written to the descriptor chain, return it.
          if (stream->used > 0) {
            stream->ReturnChain();
          }
          status = wait->Begin(dispatcher);
          FXL_CHECK(status == ZX_OK) << "Failed to wait on socket " << status;
          return;
        }
        FXL_CHECK(status == ZX_OK) << "Failed to operate on socket " << status;
      }
    }
  }

  fidl::BindingSet<fuchsia::guest::device::VirtioConsole> bindings_;
  zx_gpaddr_t trap_addr_;
  zx::event event_;
  machina::PhysMem phys_mem_;
  zx::socket socket_;

  async::GuestBellTrapMethod<VirtioConsoleImpl,
                             &VirtioConsoleImpl::OnQueueNotify>
      trap_{this};
  Stream<ZX_SOCKET_WRITABLE, &VirtioConsoleImpl::OnSocketWritable> tx_stream_{
      this};
  Stream<ZX_SOCKET_READABLE, &VirtioConsoleImpl::OnSocketReadable> rx_stream_{
      this};
};

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  VirtioConsoleImpl virtio_console(context.get());
  return loop.Run();
}
