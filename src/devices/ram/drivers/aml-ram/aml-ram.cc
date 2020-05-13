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
#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <soc/aml-s905d2/s905d2-hw.h>

namespace amlogic_ram {

constexpr size_t kMaxPendingRequests = 64u;

constexpr uint64_t kPortKeyIrqMsg = 0x0;
constexpr uint64_t kPortKeyCancelMsg = 0x1;
constexpr uint64_t kPortKeyWorkPendingMsg = 0x2;

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

  zx::interrupt irq;
  status = pdev.GetInterrupt(0, &irq);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to map interrupt, st = %d", status);
    return status;
  }

  zx::port port;
  status = zx::port::create(ZX_PORT_BIND_TO_INTERRUPT, &port);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to create port, st = %d", status);
    return status;
  }

  status = irq.bind(port, kPortKeyIrqMsg, 0 /*options*/);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to bind interrupt, st = %d", status);
    return status;
  }

  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<AmlRam>(&ac, parent, *std::move(mmio), std::move(irq),
                                                 std::move(port));
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

AmlRam::AmlRam(zx_device_t* parent, ddk::MmioBuffer mmio, zx::interrupt irq, zx::port port)
    : DeviceType(parent), mmio_(std::move(mmio)), irq_(std::move(irq)), port_(std::move(port)) {}

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

  if (!thread_.joinable()) {
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
      zx_port_packet_t packet = {
          .key = kPortKeyWorkPendingMsg, .type = ZX_PKT_TYPE_USER, .status = ZX_OK};
      ZX_ASSERT(port_.queue(&packet) == ZX_OK);
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

  // Drain stray interrupt packets from port if we exit early.
  auto auto_drain_port = fbl::MakeAutoCall([this]() {
    zx_port_packet_t packet;
    if (port_.wait(zx::time(0), &packet) == ZX_OK) {
      // Acknowledge IRQ or re-queue packet.
      if (packet.key == kPortKeyIrqMsg) {
        ZX_ASSERT(irq_.ack() == ZX_OK);
      } else {
        ZX_ASSERT(port_.queue(&packet) == ZX_OK);
      }
    }
  });

  // Disable measurement before we exit.
  // Note: must go out of scope before |auto_drain_port|.
  auto auto_disable_measurement = fbl::MakeAutoCall([this, channels_enabled]() {
    mmio_.Write32(channels_enabled | DMC_QOS_CLEAR_CTRL, MEMBW_PORTS_CTRL);
  });

  bpi->timestamp = zx_clock_get_monotonic();
  bpi->frequency = ReadFrequency();

  zx_port_packet_t packet;
  zx::time deadline = zx::deadline_after(zx::sec(1));
  for (;;) {
    auto status = port_.wait(deadline, &packet);
    if (status == ZX_ERR_TIMED_OUT) {
      return ZX_ERR_TIMED_OUT;
    }
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-ram: error in wait, st =%d\n", status);
      return status;
    }
    if (packet.key == kPortKeyCancelMsg) {
      // Shutdown. This is handled by the caller.
      return ZX_ERR_CANCELED;
    }
    if (packet.key == kPortKeyIrqMsg) {
      ZX_ASSERT(irq_.ack() == ZX_OK);
      break;
    }
    // We can ignore kPortKeyWorkPendingMsg here as caller will
    // check for more pending work before waiting on port again.
  }

  uint32_t value = mmio_.Read32(MEMBW_PORTS_CTRL);
  ZX_ASSERT((value & DMC_QOS_ENABLE_CTRL) == 0);

  bpi->channels[0].readwrite_cycles = mmio_.Read32(MEMBW_C0_GRANT_CNT) * 16ul;
  bpi->channels[1].readwrite_cycles = mmio_.Read32(MEMBW_C1_GRANT_CNT) * 16ul;
  bpi->channels[2].readwrite_cycles = mmio_.Read32(MEMBW_C2_GRANT_CNT) * 16ul;
  bpi->channels[3].readwrite_cycles = mmio_.Read32(MEMBW_C3_GRANT_CNT) * 16ul;

  auto_drain_port.cancel();
  return ZX_OK;
}

void AmlRam::ReadLoop() {
  std::deque<Job> jobs;

  for (;;) {
    zx_port_packet_t packet;
    auto status = port_.wait(zx::time::infinite(), &packet);
    if (status != ZX_OK) {
      zxlogf(ERROR, "aml-ram: error in wait, st =%d\n", status);
      return;
    }

    for (;;) {
      ZX_ASSERT(jobs.empty());

      {
        fbl::AutoLock lock(&lock_);
        if (shutdown_) {
          // Shutdown with no pending work in the local queue.
          return;
        }
        if (requests_.empty()) {
          // Done with all work. Go back to wait.
          break;
        }
        // Some work available. Move it all to the local queue.
        jobs = std::move(requests_);
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
    }
  }
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
  if (thread_.joinable()) {
    {
      fbl::AutoLock lock(&lock_);
      shutdown_ = true;
      zx_port_packet_t packet = {
          .key = kPortKeyCancelMsg, .type = ZX_PKT_TYPE_USER, .status = ZX_OK};
      ZX_ASSERT(port_.queue(&packet) == ZX_OK);
    }
    thread_.join();
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

uint64_t AmlRam::ReadFrequency() const {
  uint32_t value = mmio_.Read32(MEMBW_PLL_CNTL);
  uint64_t dpll_int_num = value & 0x1ff;
  uint64_t dpll_ref_div_n = (value >> 10) & 0x1f;
  uint64_t od = (value >> 16) & 0x7;
  uint64_t od1 = (value >> 19) & 0x1;

  ZX_ASSERT(dpll_ref_div_n);
  uint64_t od_div = 1;
  switch (od) {
    case 0:
      od_div = 2;  // 000:/2
      break;
    case 1:
      od_div = 3;  // 001:/3
      break;
    case 2:
      od_div = 4;  // 010:/4
      break;
    case 3:
      od_div = 6;  // 011:/6
      break;
    case 4:
      od_div = 8;  // 100:/8
      break;
  }
  uint64_t od1_shift = od1 == 0 ? 1 : 2;  // 0:/2, 1:/4
  // Frequency is calculated with the following equation:
  //
  // f = fREF * (M + frac) / N
  //
  constexpr uint64_t kFreqRef = 24000000;
  return (((kFreqRef * dpll_int_num) / dpll_ref_div_n) >> od1_shift) / od_div;
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
