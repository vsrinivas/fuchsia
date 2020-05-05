// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-ram.h"

#include <lib/device-protocol/pdev.h>
#include <lib/zx/clock.h>
#include <zircon/assert.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <soc/aml-s905d2/s905d2-hw.h>

namespace amlogic_ram {

constexpr zx_signals_t kCancelSignal = ZX_USER_SIGNAL_0;
constexpr zx_signals_t kWorkPendingSignal = ZX_USER_SIGNAL_1;

constexpr size_t kMaxPendingRequests = 64u;

zx_status_t ValidateRequest(const ram_metrics::BandwidthMeasurementConfig& config) {
  // Restrict timer to reasonable values.
  if ((config.cycles_to_measure < kMinimumCycleCount) ||
      (config.cycles_to_measure > kMaximumCycleCount)) {
    return ZX_ERR_INVALID_ARGS;
  }

  for (size_t ix = 0; ix != ram_metrics::MAX_COUNT_CHANNELS; ++ix) {
    auto& channel = config.channels[ix];

    if ((ix >= MEMBW_MAX_CHANNELS) && (channel != 0)) {
      // We only support the first four channels.
      return ZX_ERR_INVALID_ARGS;
    }

    if (channel > 0xffffffff) {
      // We don't support sub-ports (bits above 31) yet.
      return ZX_ERR_NOT_SUPPORTED;
    }
  }

  return ZX_OK;
}

zx_status_t AmlRam::Create(void* context, zx_device_t* parent) {
  zx_status_t status;
  ddk::PDev pdev(parent);
  std::optional<ddk::MmioBuffer> mmio;
  if ((status = pdev.MapMmio(0, &mmio)) != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to map mmio, st = %d", status);
    return status;
  }

  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<AmlRam>(&ac, parent, *std::move(mmio));
  if (!ac.check()) {
    zxlogf(ERROR, "aml-ram: Failed to allocate device memory");
    return ZX_ERR_NO_MEMORY;
  }

  status = device->DdkAdd("ram", DEVICE_ADD_NON_BINDABLE);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to add ram device, st = %d", status);
    return status;
  }

  __UNUSED auto* dummy = device.release();
  return ZX_OK;
}

AmlRam::AmlRam(zx_device_t* parent, ddk::MmioBuffer mmio)
    : DeviceType(parent), mmio_(std::move(mmio)) {}

AmlRam::~AmlRam() {
  // Verify we drained all requests.
  ZX_ASSERT(requests_.empty());
}

void AmlRam::DdkSuspend(ddk::SuspendTxn txn) {
  // TODO(cpu): First put the device into txn.requested_state().
  if (txn.suspend_reason() & (DEVICE_SUSPEND_REASON_POWEROFF | DEVICE_SUSPEND_REASON_MEXEC |
                              DEVICE_SUSPEND_REASON_REBOOT)) {
    // Do any additional cleanup that is needed while shutting down the driver.
    Shutdown();
  }
  txn.Reply(ZX_OK, txn.requested_state());
}

void AmlRam::DdkRelease() {
  Shutdown();
  delete this;
}

zx_status_t AmlRam::DdkMessage(fidl_msg_t* msg, fidl_txn_t* txn) {
  DdkTransaction transaction(txn);
  ram_metrics::Device::Dispatch(this, msg, &transaction);
  return transaction.Status();
}

void AmlRam::MeasureBandwidth(ram_metrics::BandwidthMeasurementConfig config,
                              MeasureBandwidthCompleter::Sync completer) {
  zx_status_t st = ValidateRequest(config);
  if (st != ZX_OK) {
    zxlogf(ERROR, "aml-ram: bad request\n");
    completer.ReplyError(st);
    return;
  }

  if (!thread_control_) {
    // First request.
    auto status = zx::event::create(0u, &thread_control_);
    if (status != ZX_OK) {
      completer.ReplyError(st);
      zxlogf(ERROR, "aml-ram: could not create event\n");
      return;
    }
    thread_ = std::thread([this] { ReadLoop(); });
  }

  {
    fbl::AutoLock lock(&lock_);

    if (requests_.size() > kMaxPendingRequests) {
      // Once the queue is shorter the request would likely succeed.
      completer.ReplyError(ZX_ERR_SHOULD_WAIT);
      return;
    }

    // Enqueue task and signal worker thread as needed.
    requests_.emplace_back(std::move(config), completer.ToAsync());
    if (requests_.size() == 1u) {
      thread_control_.signal(0, kWorkPendingSignal);
    }
  }
}

zx_status_t AmlRam::ReadBandwithCounters(const ram_metrics::BandwidthMeasurementConfig& config,
                                         ram_metrics::BandwidthInfo* bpi) {
  uint32_t channels_enabled = 0u;
  for (size_t ix = 0; ix != MEMBW_MAX_CHANNELS; ++ix) {
    channels_enabled |= (config.channels[ix] != 0) ? (1u << ix) : 0;
    mmio_.Write32(static_cast<uint32_t>(config.channels[ix]), MEMBW_RP[ix]);
    mmio_.Write32(0xffff, MEMBW_SP[ix]);
  }

  if (channels_enabled == 0x0) {
    // Nothing to monitor.
    return ZX_OK;
  }

  mmio_.Write32(static_cast<uint32_t>(config.cycles_to_measure), MEMBW_TIMER);
  mmio_.Write32(channels_enabled | DMC_QOS_ENABLE_CTRL, MEMBW_PORTS_CTRL);

  bpi->timestamp = zx_clock_get_monotonic();

  uint32_t value = mmio_.Read32(MEMBW_PORTS_CTRL);
  uint32_t count = 0;

  // Polling loop for the bandwith cycle counters.
  // TODO(cpu): tune wait interval to minimize polling.
  while (value & DMC_QOS_ENABLE_CTRL) {
    if (count++ > 20) {
      return ZX_ERR_TIMED_OUT;
    }

    auto status =
        thread_control_.wait_one(kCancelSignal, zx::deadline_after(zx::msec(50)), nullptr);
    if (status != ZX_ERR_TIMED_OUT) {
      // Shutdown. This is handled by the caller.
      return ZX_ERR_CANCELED;
    }
    value = mmio_.Read32(MEMBW_PORTS_CTRL);
  }

  bpi->channels[0].readwrite_cycles = mmio_.Read32(MEMBW_C0_GRANT_CNT) * 16ul;
  bpi->channels[1].readwrite_cycles = mmio_.Read32(MEMBW_C1_GRANT_CNT) * 16ul;
  bpi->channels[2].readwrite_cycles = mmio_.Read32(MEMBW_C2_GRANT_CNT) * 16ul;
  bpi->channels[3].readwrite_cycles = mmio_.Read32(MEMBW_C3_GRANT_CNT) * 16ul;

  mmio_.Write32(channels_enabled | DMC_QOS_CLEAR_CTRL, MEMBW_PORTS_CTRL);
  return ZX_OK;
}

void AmlRam::ReadLoop() {
  std::deque<Job> jobs;

  for (;;) {
    zx_signals_t observed = 0u;
    auto status = thread_control_.wait_one(kCancelSignal | kWorkPendingSignal, zx::time::infinite(),
                                           &observed);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-ram: error in wait_one, st =%d\n", status);
      return;
    }

    ZX_ASSERT(jobs.empty());

    if (observed & kCancelSignal) {
      // Shutdown with no pending work in the local queue.
      return;
    }

    {
      fbl::AutoLock lock(&lock_);
      if (requests_.empty()) {
        // Done with all work. Clear pending-work signal, go back to wait.
        thread_control_.signal(kWorkPendingSignal, 0);
        continue;
      } else {
        // Some work available. Move it all to the local queue.
        jobs = std::move(requests_);
      }
    }

    while (!jobs.empty()) {
      Job& job = jobs.front();
      ram_metrics::BandwidthInfo bpi;
      status = ReadBandwithCounters(job.config, &bpi);
      if (status == ZX_OK) {
        job.completer.ReplySuccess(bpi);
      } else if (status == ZX_ERR_CANCELED) {
        // Shutdown case with pending work in local queue. Give back the jobs
        // to the main thread.
        RevertJobs(&jobs);
        return;
      } else {
        // Unexpected error. Better log it.
        zxlogf(ERROR, "aml-ram: read error z %d\n", status);
        job.completer.ReplyError(status);
      }
      jobs.pop_front();
    }
  };
}

// Merge back the request jobs from the local jobs in |source| preserving
// the order of arrival: the last job in |source| is ahead of the
// first job in |request_|.
void AmlRam::RevertJobs(std::deque<AmlRam::Job>* source) {
  fbl::AutoLock lock(&lock_);
  while (!source->empty()) {
    requests_.push_front(std::move(source->back()));
    source->pop_back();
  }
}

void AmlRam::Shutdown() {
  if (thread_control_) {
    thread_control_.signal(kWorkPendingSignal, kCancelSignal);
    thread_.join();
    thread_control_.reset();
    // Cancel all pending requests. There are no more threads
    // but we still take the lock to keep lock checker happy.
    {
      fbl::AutoLock lock(&lock_);
      for (auto& request : requests_) {
        request.completer.Close(ZX_ERR_CANCELED);
      }
      requests_.clear();
    }
  }
}

}  // namespace amlogic_ram

static constexpr zx_driver_ops_t aml_ram_driver_ops = []() {
  zx_driver_ops_t result = {};
  result.version = DRIVER_OPS_VERSION;
  result.bind = amlogic_ram::AmlRam::Create;

  return result;
}();

// clang-format off
ZIRCON_DRIVER_BEGIN(aml_ram, aml_ram_driver_ops, "zircon", "0.1", 5)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_RAM_CTL),
    // This driver can likely support S905D3 in the future.
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_S905D2),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_PID, PDEV_PID_AMLOGIC_T931),
ZIRCON_DRIVER_END(aml_ram)
    // clang-format on
