// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-ram.h"

#include <lib/ddk/debug.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <lib/device-protocol/pdev.h>
#include <lib/zx/clock.h>
#include <zircon/assert.h>

#include <ddktl/fidl.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <soc/aml-s905d2/s905d2-hw.h>

#include "src/devices/ram/drivers/aml-ram/aml_ram_bind.h"

namespace amlogic_ram {

constexpr size_t kMaxPendingRequests = 64u;

constexpr uint64_t kPortKeyIrqMsg = 0x0;
constexpr uint64_t kPortKeyCancelMsg = 0x1;
constexpr uint64_t kPortKeyWorkPendingMsg = 0x2;

// each axi cycle transfer 4 external bus
// 16bit external bus width: kBytesPerCycle = 2 * 4 = 8
// 32bit external bus width: kBytesPerCycle = 4 * 4 = 16
constexpr uint64_t kBytesPerCycle = 16ul;
constexpr uint64_t kBytesPerCycleA1 = 8ul;

zx_status_t ValidateRequest(const ram_metrics::wire::BandwidthMeasurementConfig& config) {
  // Restrict timer to reasonable values.
  if ((config.cycles_to_measure < kMinimumCycleCount) ||
      (config.cycles_to_measure > kMaximumCycleCount)) {
    return ZX_ERR_INVALID_ARGS;
  }

  int enabled_count = 0;

  for (size_t ix = 0; ix != ram_metrics::wire::kMaxCountChannels; ++ix) {
    auto& channel = config.channels[ix];

    if ((ix >= MEMBW_MAX_CHANNELS) && (channel != 0)) {
      // We only support the first four channels.
      return ZX_ERR_INVALID_ARGS;
    }

    if (channel > 0xffffffff) {
      // We don't support sub-ports (bits above 31) yet.
      return ZX_ERR_NOT_SUPPORTED;
    }

    if (channel != 0) {
      ++enabled_count;
    }
  }

  // At least one channel had at least one port.
  if (enabled_count == 0) {
    return ZX_ERR_INVALID_ARGS;
  }

  return ZX_OK;
}

zx_status_t AmlRam::Create(void* context, zx_device_t* parent) {
  zx_status_t status;
  ddk::PDev pdev(parent);
  std::optional<fdf::MmioBuffer> mmio;
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

  pdev_device_info_t info;
  status = pdev.GetDeviceInfo(&info);
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to get device info, st = %d", status);
    return status;
  }

  zx::resource smc_monitor = {};
  if (info.pid == PDEV_PID_AMLOGIC_A5) {
    status = pdev.GetSmc(0, &smc_monitor);
    if (status != ZX_OK) {
      zxlogf(ERROR, "unable to get sip monitor handle: %s", zx_status_get_string(status));
      return status;
    }
  }

  std::optional<fdf::MmioBuffer> clk_mmio;
  if (info.pid == PDEV_PID_AMLOGIC_A1) {
    if ((status = pdev.MapMmio(1, &clk_mmio)) != ZX_OK) {
      zxlogf(ERROR, "Failed to map mmio: %s", zx_status_get_string(status));
      return status;
    }
  }

  fbl::AllocChecker ac;
  auto device = fbl::make_unique_checked<AmlRam>(&ac, parent, *std::move(mmio),
                                                 *std::move(clk_mmio), std::move(irq),
                                                 std::move(port), std::move(smc_monitor), info.pid);
  if (!ac.check()) {
    zxlogf(ERROR, "aml-ram: Failed to allocate device memory");
    return ZX_ERR_NO_MEMORY;
  }

  status = device->DdkAdd(ddk::DeviceAddArgs("ram")
                              .set_flags(DEVICE_ADD_NON_BINDABLE)
                              .set_proto_id(ZX_PROTOCOL_AMLOGIC_RAM));
  if (status != ZX_OK) {
    zxlogf(ERROR, "aml-ram: Failed to add ram device, st = %d", status);
    return status;
  }

  // It's now the responsibility of |DdkRelease| to free this object.
  __UNUSED auto* dummy = device.release();
  return ZX_OK;
}

AmlRam::AmlRam(zx_device_t* parent, fdf::MmioBuffer mmio, fdf::MmioBuffer clk_mmio,
               zx::interrupt irq, zx::port port, zx::resource smc_monitor, uint32_t device_pid)
    : DeviceType(parent),
      mmio_(std::move(mmio)),
      clk_mmio_(std::move(clk_mmio)),
      irq_(std::move(irq)),
      port_(std::move(port)),
      smc_monitor_(std::move(smc_monitor)),
      device_pid_(device_pid) {
  // TODO(fxbug.dev/53325): ALL_GRANT counter is broken on S905D2.
  all_grant_broken_ = device_pid == PDEV_PID_AMLOGIC_S905D2;

  // Read windowing data:
  // The S905D2 and the T931 both support the DMC_STICKY_1 register, which is where the
  // DDR Windowing tool writes its results.
  if (device_pid == PDEV_PID_AMLOGIC_S905D2 || device_pid == PDEV_PID_AMLOGIC_T931) {
    windowing_data_supported_ = true;
  } else {
    windowing_data_supported_ = false;
  }

  switch (device_pid_) {
    case PDEV_PID_AMLOGIC_A5:
      dmc_offsets_ = a5_dmc_regs;
      break;
    case PDEV_PID_AMLOGIC_A1:
      dmc_offsets_ = a1_dmc_regs;
      break;
    case PDEV_PID_AMLOGIC_S905D2:
    case PDEV_PID_AMLOGIC_T931:
    case PDEV_PID_AMLOGIC_S905D3:
      dmc_offsets_ = g12_dmc_regs;
      break;
    default:
      ZX_ASSERT_MSG(0, "Unsupport yet");
  }
}

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

void AmlRam::MeasureBandwidth(MeasureBandwidthRequestView request,
                              MeasureBandwidthCompleter::Sync& completer) {
  zx_status_t st = ValidateRequest(request->config);
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
    requests_.emplace_back(std::move(request->config), completer.ToAsync());
    if (requests_.size() == 1u) {
      zx_port_packet_t packet = {
          .key = kPortKeyWorkPendingMsg, .type = ZX_PKT_TYPE_USER, .status = ZX_OK};
      ZX_ASSERT(port_.queue(&packet) == ZX_OK);
    }
  }
}

void AmlRam::GetDdrWindowingResults(GetDdrWindowingResultsCompleter::Sync& completer) {
  if (windowing_data_supported_) {
    completer.ReplySuccess(mmio_.Read32(DMC_STICKY_1));
  } else {
    zxlogf(ERROR, "aml-ram: windowing data is not supported\n");
    completer.ReplyError(ZX_ERR_NOT_SUPPORTED);
  }
}

void AmlRam::StartReadBandwithCounters(Job* job) {
  uint32_t channels_enabled = 0u;
  for (size_t ix = 0; ix != MEMBW_MAX_CHANNELS; ++ix) {
    channels_enabled |= (job->config.channels[ix] != 0) ? (1u << ix) : 0;
    mmio_.Write32(static_cast<uint32_t>(job->config.channels[ix]), dmc_offsets_.ctrl1_offset[ix]);
    mmio_.Write32(0xffff, dmc_offsets_.ctrl2_offset[ix]);
  }

  job->start_time = zx_clock_get_monotonic();
  mmio_.Write32(static_cast<uint32_t>(job->config.cycles_to_measure), dmc_offsets_.timer_offset);
  mmio_.Write32(channels_enabled | DMC_QOS_ENABLE_CTRL, dmc_offsets_.port_ctrl_offset);
}

void AmlRam::FinishReadBandwithCounters(ram_metrics::wire::BandwidthInfo* bpi,
                                        zx_time_t start_time) {
  ZX_ASSERT(irq_.ack() == ZX_OK);

  bpi->timestamp = start_time;
  bpi->frequency = ReadFrequency();
  bpi->bytes_per_cycle = (device_pid_ == PDEV_PID_AMLOGIC_A1) ? kBytesPerCycleA1 : kBytesPerCycle;

  uint32_t value = mmio_.Read32(dmc_offsets_.port_ctrl_offset);
  ZX_ASSERT((value & DMC_QOS_ENABLE_CTRL) == 0);

  bpi->channels[0].readwrite_cycles = mmio_.Read32(dmc_offsets_.bw_offset[0]);
  bpi->channels[1].readwrite_cycles = mmio_.Read32(dmc_offsets_.bw_offset[1]);
  bpi->channels[2].readwrite_cycles = mmio_.Read32(dmc_offsets_.bw_offset[2]);
  bpi->channels[3].readwrite_cycles = mmio_.Read32(dmc_offsets_.bw_offset[3]);

  bpi->total.readwrite_cycles = all_grant_broken_ ? 0 : mmio_.Read32(dmc_offsets_.all_bw_offset);

  mmio_.Write32(0x0f | DMC_QOS_CLEAR_CTRL, dmc_offsets_.port_ctrl_offset);
}

void AmlRam::CancelReadBandwithCounters() {
  mmio_.Write32(0x0f | DMC_QOS_CLEAR_CTRL, dmc_offsets_.port_ctrl_offset);
  // Here there might be a pending interrupt packet. The caller
  // is going to exit so it is immaterial if we drain it or
  // not.
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

    switch (packet.key) {
      case kPortKeyWorkPendingMsg: {
        AcceptJobs(&jobs);
        StartReadBandwithCounters(&jobs.front());
        break;
      }

      case kPortKeyIrqMsg: {
        ZX_ASSERT(!jobs.empty());
        ram_metrics::wire::BandwidthInfo bpi;
        FinishReadBandwithCounters(&bpi, jobs.front().start_time);
        Job job = std::move(jobs.front());
        jobs.pop_front();
        // Start new measurement before we reply the current one.
        if (!jobs.empty()) {
          StartReadBandwithCounters(&jobs.front());
        }
        job.completer.ReplySuccess(bpi);
        break;
      }

      case kPortKeyCancelMsg: {
        if (!jobs.empty()) {
          CancelReadBandwithCounters();
          RevertJobs(&jobs);
        }
        return;
      }

      default: {
        ZX_ASSERT(false);
      }
    }
  }
}

// Merge back the request jobs from the local jobs in |source| preserving
// the order of arrival: the last job in |source| is ahead of the
// first job in |request_|.
void AmlRam::RevertJobs(std::deque<AmlRam::Job>* source) {
  fbl::AutoLock lock(&lock_);
  requests_.insert(requests_.begin(), std::make_move_iterator(source->begin()),
                   std::make_move_iterator(source->end()));
  source->clear();
}

// Merge requests from |request_| into local jobs while preserving order
// of arrival.
void AmlRam::AcceptJobs(std::deque<AmlRam::Job>* dest) {
  fbl::AutoLock lock(&lock_);
  dest->insert(dest->end(), std::make_move_iterator(requests_.begin()),
               std::make_move_iterator(requests_.end()));
  requests_.clear();
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

uint32_t AmlRam::DmcSmcRead(uint32_t addr) const {
  ZX_ASSERT(smc_monitor_.is_valid());
  static constexpr uint64_t dmc_read = 0;
  static const zx_smc_parameters_t kGetPllCtl0Call =
      amlogic_ram::CreateDmcSmcCall(addr, 0, dmc_read);

  union {
    zx_smc_result_t raw;
    uint32_t value;
  } result;
  zx_status_t status = zx_smc_call(smc_monitor_.get(), &kGetPllCtl0Call, &result.raw);
  ZX_ASSERT(status == ZX_OK);

  return result.value;
}

uint64_t AmlRam::ReadFrequency() const {
  uint32_t value = {};

  switch (device_pid_) {
    case PDEV_PID_AMLOGIC_A5:
      value = DmcSmcRead(dmc_offsets_.pll_ctrl0_reg);
      break;
    case PDEV_PID_AMLOGIC_A1:
      return DmcClockA1::Get(dmc_offsets_.pll_ctrl0_offset).ReadFrom(&clk_mmio_).Rate();
      break;
    case PDEV_PID_AMLOGIC_S905D2:
    case PDEV_PID_AMLOGIC_T931:
    case PDEV_PID_AMLOGIC_S905D3:
      value = mmio_.Read32(dmc_offsets_.pll_ctrl0_offset);
      break;
    default:
      ZX_ASSERT_MSG(0, "Unsupport yet");
  }

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
ZIRCON_DRIVER(aml_ram, aml_ram_driver_ops, "zircon", "0.1");
// clang-format on
