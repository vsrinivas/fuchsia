// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/trace-provider/provider.h>

#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/stream_base.h"

class RngStream : public StreamBase {
 public:
  void Notify() {
    for (; queue_.NextChain(&chain_); chain_.Return()) {
      while (chain_.NextDescriptor(&desc_)) {
        FX_CHECK(desc_.writable) << "Descriptor is not writable";
        zx_cprng_draw(desc_.addr, desc_.len);
        *chain_.Used() += desc_.len;
      }
    }
  }
};

// Implementation of a virtio-rng device.
class VirtioRngImpl : public DeviceBase<VirtioRngImpl>,
                      public fuchsia::virtualization::hardware::VirtioRng {
 public:
  VirtioRngImpl(sys::ComponentContext* context) : DeviceBase(context) {}

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    FX_CHECK(queue == 0) << "Queue index " << queue << " out of range";
    queue_.Notify();
  }

 private:
  // |fuchsia::virtualization::hardware::VirtioRng|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info,
             StartCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    PrepStart(std::move(start_info));
    queue_.Init(phys_mem_,
                fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioRngImpl::Interrupt));
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                      zx_gpaddr_t used, ConfigureQueueCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    FX_CHECK(queue == 0) << "Queue index " << queue << " out of range";
    queue_.Configure(size, desc, avail, used);
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void Ready(uint32_t negotiated_features, ReadyCallback callback) override { callback(); }

  RngStream queue_;
};

int main(int argc, char** argv) {
  syslog::InitLogger({"virtio_rng"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();

  VirtioRngImpl virtio_rng(context.get());
  return loop.Run();
}
