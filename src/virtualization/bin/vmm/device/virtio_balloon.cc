// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/trace-provider/provider.h>

#include <virtio/balloon.h>

#include "src/virtualization/bin/vmm/device/device_base.h"
#include "src/virtualization/bin/vmm/device/stream_base.h"

// From Virtio 1.0, Section 5.5.6: This value is historical, and independent
// of the guest page size.
static constexpr uint32_t kPageSize = 4096;

// Limit the number of callbacks so that the device process can not be exhausted
// of memory by requests for memory statistics.
static constexpr size_t kCallbackLimit = 8;

using GetMemStatsCallback = fuchsia::virtualization::hardware::VirtioBalloon::GetMemStatsCallback;

enum class Queue : uint16_t {
  INFLATE = 0,
  DEFLATE = 1,
  STATS = 2,
};

// Stream for inflate and deflate queues.
class BalloonStream : public StreamBase {
 public:
  void DoBalloon(const zx::vmo& vmo, uint32_t op) {
    for (; queue_.NextChain(&chain_); chain_.Return()) {
      while (chain_.NextDescriptor(&desc_)) {
        zx_status_t status = DoOperation(vmo, op);
        FX_CHECK(status == ZX_OK) << "Operation failed " << status;
      }
    }
  }

 private:
  // Handle balloon inflate/deflate requests. From Virtio 1.0, Section 5.5.6:
  //
  // To supply memory to the balloon (aka. inflate):
  //  (a) The driver constructs an array of addresses of unused memory pages.
  //      These addresses are divided by 4096 and the descriptor describing the
  //      resulting 32-bit array is added to the inflateq.
  //
  // To remove memory from the balloon (aka. deflate):
  //  (a) The driver constructs an array of addresses of memory pages it has
  //      previously given to the balloon, as described above. This descriptor
  //      is added to the deflateq.
  //  (b) If the VIRTIO_BALLOON_F_MUST_TELL_HOST feature is negotiated, the
  //      guest informs the device of pages before it uses them.
  //  (c) Otherwise, the guest is allowed to re-use pages previously given to
  //      the balloon before the device has acknowledged their withdrawal.
  zx_status_t DoOperation(const zx::vmo& vmo, uint32_t op) {
    auto pfns = static_cast<uint32_t*>(desc_.addr);
    auto num_pfns = desc_.len / 4;

    // If the driver writes contiguous PFNs, we will combine them into runs.
    uint64_t base = 0;
    uint64_t run = 0;
    for (uint32_t i = 0; i < num_pfns; i++) {
      if (run > 0) {
        // If this is part of the current run, extend it.
        if (base + run == pfns[i]) {
          run++;
          continue;
        }
        // We have completed a run, so process it before starting a new run.
        zx_status_t status = vmo.op_range(op, base * kPageSize, run * kPageSize, nullptr, 0);
        if (status != ZX_OK) {
          return status;
        }
      }
      // Start a new run.
      base = pfns[i];
      run = 1;
    }
    if (run == 0) {
      return ZX_OK;
    }
    // Process the final run.
    return vmo.op_range(op, base * kPageSize, run * kPageSize, nullptr, 0);
  }
};

// Stream for stats queue.
class StatsStream : public StreamBase {
 public:
  void GetMemStats(GetMemStatsCallback callback) {
    if (callbacks_.size() >= kCallbackLimit) {
      // If we have reached our limit for queued callbacks, return.
      callback(ZX_ERR_SHOULD_WAIT, nullptr);
      return;
    } else if (!chain_.IsValid()) {
      // If this is the first time memory statistics are requested, fetch a
      // descriptor chain from the queue.
      if (!queue_.NextChain(&chain_)) {
        // If we do not have a descriptor chain in the queue, the device is not
        // ready, therefore return.
        callback(ZX_ERR_SHOULD_WAIT, nullptr);
        return;
      }
    }
    chain_.Return();
    callbacks_.emplace_back(std::move(callback));
  }

  void DoStats() {
    if (callbacks_.empty()) {
      return;
    }

    zx_status_t status = ZX_ERR_IO_DATA_INTEGRITY;
    std::vector<fuchsia::virtualization::MemStat> mem_stats;
    if (queue_.NextChain(&chain_) && chain_.NextDescriptor(&desc_) &&
        desc_.len % sizeof(virtio_balloon_stat_t) == 0) {
      auto stats = static_cast<virtio_balloon_stat_t*>(desc_.addr);
      size_t len = desc_.len / sizeof(virtio_balloon_stat_t);
      for (size_t i = 0; i < len; i++) {
        mem_stats.push_back({
            .tag = stats[i].tag,
            .val = stats[i].val,
        });
      }
      status = ZX_OK;
    }

    for (auto& callback : callbacks_) {
      callback(status, fidl::Clone(mem_stats));
    }
    callbacks_.clear();
  }

 private:
  std::vector<GetMemStatsCallback> callbacks_;
};

// Implementation of a virtio-balloon device.
class VirtioBalloonImpl : public DeviceBase<VirtioBalloonImpl>,
                          public fuchsia::virtualization::hardware::VirtioBalloon {
 public:
  VirtioBalloonImpl(sys::ComponentContext* context) : DeviceBase(context) {}

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    switch (static_cast<Queue>(queue)) {
      case Queue::INFLATE:
        inflate_stream_.DoBalloon(phys_mem_.vmo(), ZX_VMO_OP_DECOMMIT);
        break;
      case Queue::DEFLATE:
        deflate_stream_.DoBalloon(phys_mem_.vmo(), ZX_VMO_OP_COMMIT);
        break;
      case Queue::STATS:
        stats_stream_.DoStats();
        break;
      default:
        FX_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

 private:
  // |fuchsia::virtualization::hardware::VirtioBalloon|
  void Start(fuchsia::virtualization::hardware::StartInfo start_info,
             StartCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    PrepStart(std::move(start_info));
    inflate_stream_.Init(
        phys_mem_, fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioBalloonImpl::Interrupt));
    deflate_stream_.Init(
        phys_mem_, fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioBalloonImpl::Interrupt));
    stats_stream_.Init(
        phys_mem_, fit::bind_member<zx_status_t, DeviceBase>(this, &VirtioBalloonImpl::Interrupt));
  }

  // |fuchsia::virtualization::hardware::VirtioBalloon|
  void GetMemStats(GetMemStatsCallback callback) override {
    if (!(negotiated_features_ & VIRTIO_BALLOON_F_STATS_VQ)) {
      // If memory statistics are not supported, return.
      callback(ZX_ERR_NOT_SUPPORTED, nullptr);
    } else {
      stats_stream_.GetMemStats(std::move(callback));
    }
  }

  // |fuchsia::virtualization::hardware::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc, zx_gpaddr_t avail,
                      zx_gpaddr_t used, ConfigureQueueCallback callback) override {
    auto deferred = fit::defer(std::move(callback));
    switch (static_cast<Queue>(queue)) {
      case Queue::INFLATE:
        inflate_stream_.Configure(size, desc, avail, used);
        break;
      case Queue::DEFLATE:
        deflate_stream_.Configure(size, desc, avail, used);
        break;
      case Queue::STATS:
        stats_stream_.Configure(size, desc, avail, used);
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

  uint32_t negotiated_features_;
  BalloonStream inflate_stream_;
  BalloonStream deflate_stream_;
  StatsStream stats_stream_;
};

int main(int argc, char** argv) {
  syslog::InitLogger({"virtio_balloon"});

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> context = sys::ComponentContext::Create();

  VirtioBalloonImpl virtio_balloon(context.get());
  return loop.Run();
}
