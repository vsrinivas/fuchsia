// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RAM_DRIVERS_AML_RAM_AML_RAM_H_
#define SRC_DEVICES_RAM_DRIVERS_AML_RAM_AML_RAM_H_

#include <fidl/fuchsia.device/cpp/wire.h>
#include <fidl/fuchsia.hardware.ram.metrics/cpp/wire.h>
#include <lib/mmio/mmio.h>
#include <lib/zircon-internal/thread_annotations.h>
#include <lib/zx/interrupt.h>
#include <lib/zx/port.h>

#include <deque>
#include <thread>

#include <ddktl/device.h>
#include <fbl/mutex.h>

namespace ram_metrics = fuchsia_hardware_ram_metrics;

namespace amlogic_ram {

// The AmlRam device provides FIDL services directly to applications
// to query performance counters. For example effective DDR bandwith.
//
// There are 4 monitoring channels and each one can aggregate up to 64
// hardware memory ports. NOTE: the word channel and port in this file
// refer to hardware, not to zircon objects.
constexpr size_t MEMBW_MAX_CHANNELS = 4u;

// Controls start,stop and if polling or interrupt mode.
constexpr uint32_t MEMBW_PORTS_CTRL = (0x0020 << 2);
constexpr uint32_t DMC_QOS_ENABLE_CTRL = (0x01 << 31);
constexpr uint32_t DMC_QOS_CLEAR_CTRL = (0x01 << 30);

// Returns all granted cycles.
constexpr uint32_t MEMBW_ALL_GRANT_CNT = (0x2a << 2);

// Returns the granted cycles per channel.
constexpr uint32_t MEMBW_C0_GRANT_CNT = (0x2b << 2);
constexpr uint32_t MEMBW_C1_GRANT_CNT = (0x2c << 2);
constexpr uint32_t MEMBW_C2_GRANT_CNT = (0x2d << 2);
constexpr uint32_t MEMBW_C3_GRANT_CNT = (0x2e << 2);

// Controls how long to measure cycles for.
constexpr uint32_t MEMBW_TIMER = (0x002f << 2);

// Controls which ports are assigned to each channel.
constexpr uint32_t MEMBW_RP[MEMBW_MAX_CHANNELS] = {(0x0021 << 2), (0x0023 << 2), (0x0025 << 2),
                                                   (0x0027 << 2)};

// Controls which subports are assigned to each channel.
constexpr uint32_t MEMBW_SP[MEMBW_MAX_CHANNELS] = {(0x0022 << 2), (0x0024 << 2), (0x0026 << 2),
                                                   (0x0028 << 2)};

// Contains the DDR frequency.
// TODO(reveman): Understand why we use 0x0300 instead of 0x0000.
constexpr uint32_t MEMBW_PLL_CNTL = (0x0300 << 2);

// Sticky bit that holds the DDR windowing tool results
// address is: 0xff638804
// We mapped at T931_DMC_BASE (0xff638000)
constexpr uint32_t DMC_STICKY_1 = 0x804;

constexpr uint64_t kMinimumCycleCount = 1024 * 512;
constexpr uint64_t kMaximumCycleCount = 0xffffffff;

class AmlRam;
using DeviceType =
    ddk::Device<AmlRam, ddk::Suspendable, ddk::Messageable<ram_metrics::Device>::Mixin>;

class AmlRam : public DeviceType {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(AmlRam);

  static zx_status_t Create(void* context, zx_device_t* parent);

  AmlRam(zx_device_t* parent, ddk::MmioBuffer mmio, zx::interrupt irq, zx::port port,
         uint32_t device_pid);
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
  void GetDdrWindowingResults(GetDdrWindowingResultsRequestView request,
                              GetDdrWindowingResultsCompleter::Sync& completer) override;

  void StartReadBandwithCounters(Job* job);
  void FinishReadBandwithCounters(ram_metrics::wire::BandwidthInfo* bpi, zx_time_t start_time);
  void CancelReadBandwithCounters();

  zx_status_t Bind();
  void ReadLoop();
  void RevertJobs(std::deque<AmlRam::Job>* source);
  void AcceptJobs(std::deque<AmlRam::Job>* source);
  void Shutdown();
  uint64_t ReadFrequency() const;

  ddk::MmioBuffer mmio_;
  zx::interrupt irq_;
  zx::port port_;
  std::thread thread_;
  fbl::Mutex lock_;
  std::deque<Job> requests_ TA_GUARDED(lock_);
  bool shutdown_ TA_GUARDED(lock_) = false;
  bool all_grant_broken_ = true;

  bool windowing_data_supported_ = false;
};

}  // namespace amlogic_ram

#endif  // SRC_DEVICES_RAM_DRIVERS_AML_RAM_AML_RAM_H_
