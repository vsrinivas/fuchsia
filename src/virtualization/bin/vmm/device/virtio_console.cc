// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/trace-provider/provider.h>

#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/stream_base.h"

class VirtioConsoleImpl;

enum class Queue : uint16_t {
  RECEIVE = 0,
  TRANSMIT = 1,
};

// Stream for receive and transmit queues.
template <zx_signals_t Trigger, auto Method>
class ConsoleStream : public StreamBase {
 public:
  ConsoleStream(VirtioConsoleImpl* impl) : wait_(impl) {}

  void Init(const zx::socket& socket, const PhysMem& phys_mem, VirtioQueue::InterruptFn interrupt) {
    wait_.set_object(socket.get());
    wait_.set_trigger(Trigger);
    StreamBase::Init(phys_mem, std::move(interrupt));
  }

  void WaitOnSocket(async_dispatcher_t* dispatcher) {
    zx_status_t status = wait_.Begin(dispatcher);
    FX_CHECK(status == ZX_OK || status == ZX_ERR_ALREADY_EXISTS)
        << "Failed to wait on socket " << status;
  }

  template <typename Function>
  void OnSocketReady(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                     Function process_descriptor) {
    // If |process_descriptor| return ZX_ERR_SHOULD_WAIT, we may be in the
    // middle of processing a descriptor chain, therefore we should continue
    // where we left off.
    if (chain_.IsValid()) {
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
          FX_CHECK(status == ZX_OK) << "Failed to wait on socket " << status;
          return;
        }
        FX_CHECK(status == ZX_OK) << "Failed to operate on socket " << status;
      }
    }
  }

 private:
  async::WaitMethod<VirtioConsoleImpl, Method> wait_;
};

// Implementation of a virtio-console device.
class VirtioConsoleImpl : public DeviceBase<VirtioConsoleImpl>,
                          public fuchsia::virtualization::hardware::VirtioConsole {
 public:
  VirtioConsoleImpl(sys::ComponentContext* context) : DeviceBase(context) {}

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    switch (static_cast<Queue>(queue)) {
      case Queue::RECEIVE:
        rx_stream_.WaitOnSocket(async_get_default_dispatcher());
        break;
      case Queue::TRANSMIT:
        tx_stream_.WaitOnSocket(async_get_default_dispatcher());
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

 private:
  // |fuchsia::virtualization::hardware::VirtioConsole|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info, zx::socket socket,
             StartCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    PrepStart(std::move(start_info));
    socket_ = std::move(socket);
    rx_stream_.Init(socket_, phys_mem_,
                    fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioConsoleImpl::Interrupt));
    tx_stream_.Init(socket_, phys_mem_,
                    fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioConsoleImpl::Interrupt));
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
  void Ready(uint32_t negotiated_features, ReadyCallback callback) override { callback(); }

  void OnSocketReadable(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
    FX_CHECK(status == ZX_OK) << "Wait for socket readable failed " << status;
    rx_stream_.OnSocketReady(dispatcher, wait, [this](auto desc) {
      FX_CHECK(desc->writable) << "Descriptor is not writable";
      size_t actual = 0;
      zx_status_t status = socket_.read(0, desc->addr, desc->len, &actual);
      *rx_stream_.Used() += actual;
      return status;
    });
  }

  void OnSocketWritable(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                        const zx_packet_signal_t* signal) {
    FX_CHECK(status == ZX_OK) << "Wait for socket writable failed " << status;
    tx_stream_.OnSocketReady(dispatcher, wait, [this](auto desc) {
      FX_CHECK(!desc->writable) << "Descriptor is not readable";
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

  zx::socket socket_;
  ConsoleStream<ZX_SOCKET_READABLE, &VirtioConsoleImpl::OnSocketReadable> rx_stream_{this};
  ConsoleStream<ZX_SOCKET_WRITABLE, &VirtioConsoleImpl::OnSocketWritable> tx_stream_{this};
};

int main(int argc, char** argv) {
  syslog::InitLogger({"virtio_console"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();

  VirtioConsoleImpl virtio_console(context.get());
  return loop.Run();
}
