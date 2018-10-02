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
#include <virtio/balloon.h>

#include "garnet/lib/machina/device/config.h"
#include "garnet/lib/machina/device/stream_base.h"

// Per Virtio 1.0 Section 5.5.6, This value is historical, and independent
// of the guest page size.
static constexpr uint32_t kPageSize = 4096;

// Limit the number of callbacks so that the device process can not be exhausted
// of memory by requests for memory statistics.
static constexpr size_t kCallbackLimit = 8;

enum class Queue : uint16_t {
  INFLATE = 0,
  DEFLATE = 1,
  STATS = 2,
};

// Implementation of a virtio-balloon device.
class VirtioBalloonImpl : public fuchsia::guest::device::VirtioBalloon {
 public:
  VirtioBalloonImpl(component::StartupContext* context) {
    context->outgoing().AddPublicService(bindings_.GetHandler(this));
  }

 private:
  // |fuchsia::guest::device::VirtioBalloon|
  void Start(fuchsia::guest::device::StartInfo start_info,
             bool demand_page) override {
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

    demand_page_ = demand_page;
    inflate_queue_.Init(phys_mem_,
                        fit::bind_member(this, &VirtioBalloonImpl::Interrupt));
    deflate_queue_.Init(phys_mem_,
                        fit::bind_member(this, &VirtioBalloonImpl::Interrupt));
    stats_queue_.Init(phys_mem_,
                      fit::bind_member(this, &VirtioBalloonImpl::Interrupt));
  }

  // |fuchsia::guest::device::VirtioBalloon|
  void GetMemStats(GetMemStatsCallback callback) override {
    if (!(negotiated_features_ & VIRTIO_BALLOON_F_STATS_VQ)) {
      // If memory statistics are not supported, return.
      callback(ZX_ERR_NOT_SUPPORTED, nullptr);
      return;
    } else if (callbacks_.size() >= kCallbackLimit) {
      // If we have reached our limit for queued callbacks, return.
      callback(ZX_ERR_SHOULD_WAIT, nullptr);
      return;
    } else if (!stats_queue_.HasDescriptor()) {
      // If this is the first time memory statistics are requested, fetch a
      // descriptor chain from the queue.
      if (!stats_queue_.HasChain()) {
        // If we do not have a descriptor chain in the queue, the device is not
        // ready, therefore return.
        callback(ZX_ERR_SHOULD_WAIT, nullptr);
        return;
      }
      stats_queue_.NextDescriptor();
    }
    stats_queue_.ReturnChain();
    callbacks_.emplace_back(std::move(callback));
  }

  // |fuchsia::guest::device::VirtioDevice|
  void ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                      zx_gpaddr_t avail, zx_gpaddr_t used) override {
    machina::StreamBase* stream;
    switch (static_cast<Queue>(queue)) {
      case Queue::INFLATE:
        stream = &inflate_queue_;
        break;
      case Queue::DEFLATE:
        stream = &deflate_queue_;
        break;
      case Queue::STATS:
        stream = &stats_queue_;
        break;
      default:
        FXL_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
    stream->queue.Configure(size, desc, avail, used);
  }

  // |fuchsia::guest::device::VirtioDevice|
  void NotifyQueue(uint16_t queue) override {
    switch (static_cast<Queue>(queue)) {
      case Queue::INFLATE:
        return DoBalloon(&inflate_queue_, ZX_VMO_OP_DECOMMIT);
      case Queue::DEFLATE:
        if (demand_page_) {
          // If demand paging is preferred, ignore the deflate queue when
          // processing notifications.
          return;
        }
        return DoBalloon(&deflate_queue_, ZX_VMO_OP_COMMIT);
      case Queue::STATS:
        return DoStats(&stats_queue_);
      default:
        FXL_CHECK(false) << "Queue index " << queue << " out of range";
        __UNREACHABLE;
    }
  }

  // |fuchsia::guest::device::VirtioDevice|
  void Ready(uint32_t negotiated_features) override {
    negotiated_features_ = negotiated_features;
  }

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
    NotifyQueue(queue);
  }

  void DoBalloon(machina::StreamBase* stream, uint32_t op) {
    for (; stream->HasChain(); stream->ReturnChain()) {
      while (stream->NextDescriptor()) {
        zx_status_t status =
            DoBalloonOp(op, stream->desc.addr, stream->desc.len);
        FXL_CHECK(status == ZX_OK) << "Operation failed " << status;
      }
    }
  }

  // Handle balloon inflate/deflate requests. From VIRTIO 1.0 Section 5.5.6:
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
  zx_status_t DoBalloonOp(uint32_t op, void* addr, uint32_t len) {
    auto pfns = static_cast<uint32_t*>(addr);
    auto num_pfns = len / 4;

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
        zx_status_t status = phys_mem_.vmo().op_range(
            op, base * kPageSize, run * kPageSize, nullptr, 0);
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
    return phys_mem_.vmo().op_range(op, base * kPageSize, run * kPageSize,
                                    nullptr, 0);
  }

  void DoStats(machina::StreamBase* stream) {
    if (callbacks_.empty()) {
      return;
    }

    stats_queue_.NextDescriptor();
    zx_status_t status = ZX_ERR_IO_DATA_INTEGRITY;
    fidl::VectorPtr<fuchsia::guest::MemStat> mem_stats;
    if (stats_queue_.desc.len % sizeof(virtio_balloon_stat_t) == 0) {
      auto stats = static_cast<virtio_balloon_stat_t*>(stats_queue_.desc.addr);
      size_t len = stats_queue_.desc.len / sizeof(virtio_balloon_stat_t);
      for (size_t i = 0; i < len; i++) {
        mem_stats.push_back({
            .tag = stats[i].tag,
            .val = stats[i].val,
        });
      }
      status = ZX_OK;
    }

    for (auto& callback : callbacks_) {
      callback(status, mem_stats.Clone());
    }
    callbacks_.clear();
  }

  fidl::BindingSet<VirtioBalloon> bindings_;
  zx_gpaddr_t trap_addr_;
  zx::event event_;
  machina::PhysMem phys_mem_;
  bool demand_page_;
  uint32_t negotiated_features_;
  std::vector<GetMemStatsCallback> callbacks_;

  async::GuestBellTrapMethod<VirtioBalloonImpl,
                             &VirtioBalloonImpl::OnQueueNotify>
      trap_{this};
  machina::StreamBase inflate_queue_;
  machina::StreamBase deflate_queue_;
  machina::StreamBase stats_queue_;
};

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  VirtioBalloonImpl virtio_balloon(context.get());
  return loop.Run();
}
