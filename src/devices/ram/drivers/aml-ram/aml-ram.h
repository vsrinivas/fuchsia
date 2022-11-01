// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RAM_DRIVERS_AML_RAM_AML_RAM_H_
#define SRC_DEVICES_RAM_DRIVERS_AML_RAM_AML_RAM_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.ram.metrics/cpp/wire.h>
#include <lib/ddk/platform-defs.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>
#include <zircon/syscalls/smc.h>

#include <deque>
#include <thread>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <hwreg/bitfields.h>

namespace ram_metrics = fuchsia_hardware_ram_metrics;

namespace amlogic_ram {

// The AmlRam device provides FIDL services directly to applications
// to query performance counters. For example effective DDR bandwith.
//
// There are 4 monitoring channels and each one can aggregate up to 64
// hardware memory ports. NOTE: the word channel and port in this file
// refer to hardware, not to zircon objects.
constexpr size_t MEMBW_MAX_CHANNELS = 4u;

// For `dmc_reg_ctl.port_ctrl_offset`
// Bit31 qos_mon_en;
// Bit30 qos_mon irq clear;
constexpr uint32_t DMC_QOS_ENABLE_CTRL = (0x01 << 31);
constexpr uint32_t DMC_QOS_CLEAR_CTRL = (0x01 << 30);

// Sticky bit that holds the DDR windowing tool results
// address is: 0xff638804
// We mapped at T931_DMC_BASE (0xff638000)
constexpr uint32_t DMC_STICKY_1 = 0x804;

constexpr uint64_t kMinimumCycleCount = 1024 * 512;
constexpr uint64_t kMaximumCycleCount = 0xffffffff;

typedef struct dmc_reg_ctl {
  uint32_t port_ctrl_offset;  // Controls start,stop and if polling or interrupt mode.
                              // Bit31 qos_mon_en;
                              // Bit30 qos_mon irq clear;
                              // Bit[x:0] BW monitor [?] enable
  uint32_t timer_offset;      // Controls how long to measure cycles for.

  // Controls which ports are assigned to each channel.
  uint32_t ctrl1_offset[MEMBW_MAX_CHANNELS];
  // Controls which subports are assigned to each channel.
  uint32_t ctrl2_offset[MEMBW_MAX_CHANNELS];
  // At the test period, Returns all granted cycles.
  uint32_t all_bw_offset;
  // At the test period, Returns the granted cycles per channel.
  uint32_t bw_offset[MEMBW_MAX_CHANNELS];
  // Contains the DDR frequency config settings.
  union {
    uint32_t pll_ctrl0_offset;  // Access with mmio read.
    uint32_t pll_ctrl0_reg;     // Access with smc call.
  };
} dmc_reg_ctl_t;

// For S905D2 / T931 / S905D3
static constexpr dmc_reg_ctl_t g12_dmc_regs = {
    .port_ctrl_offset = (0x0020 << 2),
    .timer_offset = (0x002f << 2),
    .ctrl1_offset = {(0x0021 << 2), (0x0023 << 2), (0x0025 << 2), (0x0027 << 2)},
    .ctrl2_offset = {(0x0022 << 2), (0x0024 << 2), (0x0026 << 2), (0x0028 << 2)},
    .all_bw_offset = (0x002a << 2),
    .bw_offset = {(0x002b << 2), (0x002c << 2), (0x002d << 2), (0x002e << 2)},
    .pll_ctrl0_offset = (0x0300 << 2),
};

// For A5
static constexpr dmc_reg_ctl_t a5_dmc_regs = {
    .port_ctrl_offset = (0x0020 << 2),
    .timer_offset = (0x0021 << 2),
    .ctrl1_offset = {(0x0026 << 2), (0x002a << 2), (0x002e << 2), (0x0032 << 2)},
    .ctrl2_offset = {(0x0027 << 2), (0x002b << 2), (0x002f << 2), (0x0033 << 2)},
    .all_bw_offset = (0x0023 << 2),
    .bw_offset = {(0x0028 << 2), (0x002c << 2), (0x0030 << 2), (0x0034 << 2)},
    .pll_ctrl0_reg = 0xfe040000,
};

// For A1
// Max channels = 2
static constexpr dmc_reg_ctl_t a1_dmc_regs = {
    .port_ctrl_offset = (0x0020 << 2),
    .timer_offset = (0x002f << 2),
    .ctrl1_offset = {(0x0021 << 2), (0x0023 << 2), (0x0025 << 2), (0x0027 << 2)},
    .ctrl2_offset = {(0x0022 << 2), (0x0024 << 2), (0x0026 << 2), (0x0028 << 2)},
    .all_bw_offset = (0x002a << 2),
    .bw_offset = {(0x002b << 2), (0x002c << 2), (0x002d << 2), (0x002e << 2)},
    .pll_ctrl0_reg = (0x003e << 2),
};

static constexpr zx_smc_parameters_t CreateDmcSmcCall(uint64_t arg1, uint64_t arg2, uint64_t arg3,
                                                      uint64_t arg4 = 0, uint64_t arg5 = 0,
                                                      uint64_t arg6 = 0, uint16_t client_id = 0,
                                                      uint16_t secure_os_id = 0) {
  constexpr uint32_t kDmcSecureRegisterCtrlFuncId = 0x8200004A;
  return {kDmcSecureRegisterCtrlFuncId,
          {},
          arg1,
          arg2,
          arg3,
          arg4,
          arg5,
          arg6,
          client_id,
          secure_os_id,
          {}};
}

class DmcClockA1 : public hwreg::RegisterBase<DmcClockA1, uint32_t> {
 public:
  static constexpr uint64_t kXtalClk = 24'000'000;
  static constexpr uint64_t kFixedClkDiv2 = 768'000'000;
  static constexpr uint64_t kFixedClkDiv3 = 512'000'000;
  static constexpr uint64_t kFixedClkDiv5 = 307'200'000;

  DEF_BIT(15, force_xtal);
  DEF_FIELD(10, 9, source);
  DEF_FIELD(7, 0, div);

  uint64_t Rate() const {
    if (force_xtal()) {
      return kXtalClk;
    }

    uint64_t fclk = {};
    switch (source()) {
      case 0:
        fclk = kFixedClkDiv2;
        break;
      case 1:
        fclk = kFixedClkDiv2;
        break;
      case 2:
        fclk = kFixedClkDiv2;
        break;
      default:
        return kXtalClk;
    }

    return fclk / (div() + 1);
  }

  static auto Get(uint32_t offset) { return hwreg::RegisterAddr<DmcClockA1>(offset); }
};

class AmlRam;
using DeviceType =
    ddk::Device<AmlRam, ddk::Suspendable, ddk::Messageable<ram_metrics::Device>::Mixin>;

class AmlRam : public DeviceType {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlRam);

  static zx_status_t Create(void* context, zx_device_t* parent);

  AmlRam(zx_device_t* parent, fdf::MmioBuffer mmio, fdf::MmioBuffer clk_mmio, zx::interrupt irq,
         zx::port port, zx::resource smc_monitor, uint32_t device_pid);
  ~AmlRam();
  void DdkRelease();
  void DdkSuspend(ddk::SuspendTxn txn);

 private:
  struct Job {
    ram_metrics::wire::BandwidthMeasurementConfig config;
    MeasureBandwidthCompleter::Async completer;
    zx_time_t start_time = 0;
    Job() = delete;
    Job(ram_metrics::wire::BandwidthMeasurementConfig config,
        MeasureBandwidthCompleter::Async completer)
        : config(std::move(config)), completer(std::move(completer)) {}
  };

  // Implementation of fidl::WireServer<ram_metrics::Device> FIDL service.
  void MeasureBandwidth(MeasureBandwidthRequestView request,
                        MeasureBandwidthCompleter::Sync& completer) override;
  void GetDdrWindowingResults(GetDdrWindowingResultsCompleter::Sync& completer) override;

  void StartReadBandwithCounters(Job* job);
  void FinishReadBandwithCounters(ram_metrics::wire::BandwidthInfo* bpi, zx_time_t start_time);
  void CancelReadBandwithCounters();

  zx_status_t Bind();
  void ReadLoop();
  void RevertJobs(std::deque<AmlRam::Job>* source);
  void AcceptJobs(std::deque<AmlRam::Job>* source);
  void Shutdown();
  uint32_t DmcSmcRead(uint32_t addr) const;
  uint64_t ReadFrequency() const;

  fdf::MmioBuffer mmio_;
  fdf::MmioBuffer clk_mmio_;
  zx::interrupt irq_;
  zx::port port_;
  zx::resource smc_monitor_;
  uint32_t device_pid_;

  // DMC Register Offset
  dmc_reg_ctl_t dmc_offsets_;

  std::thread thread_;
  fbl::Mutex lock_;
  std::deque<Job> requests_ TA_GUARDED(lock_);
  bool shutdown_ TA_GUARDED(lock_) = false;
  bool all_grant_broken_ = true;

  bool windowing_data_supported_ = false;
};

}  // namespace amlogic_ram

#endif  // SRC_DEVICES_RAM_DRIVERS_AML_RAM_AML_RAM_H_
