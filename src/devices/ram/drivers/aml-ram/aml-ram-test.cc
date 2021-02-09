// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-ram.h"

#include <fuchsia/hardware/platform/bus/cpp/banjo.h>
#include <fuchsia/hardware/platform/device/cpp/banjo.h>
#include <lib/device-protocol/pdev.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fake_ddk/fidl-helper.h>

#include <atomic>
#include <memory>

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <fake-mmio-reg/fake-mmio-reg.h>

#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"

namespace amlogic_ram {

constexpr size_t kRegSize = 0x01000 / sizeof(uint32_t);

class FakeMmio {
 public:
  FakeMmio() {
    regs_ = std::make_unique<ddk_fake::FakeMmioReg[]>(kRegSize);
    mmio_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.get(), sizeof(uint32_t), kRegSize);
  }

  fake_pdev::FakePDev::MmioInfo mmio_info() { return {.offset = reinterpret_cast<size_t>(this)}; }

  ddk::MmioBuffer mmio() { return ddk::MmioBuffer(mmio_->GetMmioBuffer()); }

  ddk_fake::FakeMmioReg& reg(size_t ix) {
    // AML registers are in virtual address units.
    return regs_[ix >> 2];
  }

 private:
  std::unique_ptr<ddk_fake::FakeMmioReg[]> regs_;
  std::unique_ptr<ddk_fake::FakeMmioRegRegion> mmio_;
};

class Ddk : public fake_ddk::Bind {
 public:
  Ddk() {}
  bool added() { return add_called_; }
  const device_add_args_t& args() { return add_args_; }

 private:
  zx_status_t DeviceAdd(zx_driver_t* drv, zx_device_t* parent, device_add_args_t* args,
                        zx_device_t** out) override {
    zx_status_t status = fake_ddk::Bind::DeviceAdd(drv, parent, args, out);
    if (status != ZX_OK) {
      return status;
    }
    add_args_ = *args;
    return ZX_OK;
  }
  device_add_args_t add_args_;
};

class AmlRamDeviceTest : public zxtest::Test {
 public:
  void SetUp() override {
    irq_signaller_ = pdev_.CreateVirtualInterrupt(0);

    pdev_.set_device_info(pdev_device_info_t{
        .pid = PDEV_PID_AMLOGIC_T931,
    });

    pdev_.set_mmio(0, mmio_.mmio_info());

    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_PDEV, {pdev_.proto()->ops, pdev_.proto()->ctx}};
    ddk_.SetProtocols(std::move(protocols));

    EXPECT_OK(amlogic_ram::AmlRam::Create(nullptr, fake_ddk::FakeParent()));
  }

  void TearDown() override {
    auto device = static_cast<amlogic_ram::AmlRam*>(ddk_.args().ctx);
    device->DdkAsyncRemove();
    EXPECT_TRUE(ddk_.Ok());

    device->DdkRelease();
  }

  void InjectInterrupt() { irq_signaller_->trigger(0, zx::time()); }

 protected:
  FakeMmio mmio_;
  fake_pdev::FakePDev pdev_;
  zx::unowned_interrupt irq_signaller_;
  Ddk ddk_;
};

void WriteDisallowed(uint64_t value) { EXPECT_TRUE(false, "got register write of 0x%lx", value); }

TEST_F(AmlRamDeviceTest, InitDoesNothing) {
  // By itself the driver does not write to registers.
  // The fixture's TearDown() test also the lifecycle bits.
  mmio_.reg(MEMBW_PORTS_CTRL).SetWriteCallback(&WriteDisallowed);
  mmio_.reg(MEMBW_TIMER).SetWriteCallback(&WriteDisallowed);
}

TEST_F(AmlRamDeviceTest, MalformedRequests) {
  // An invalid request does not write to registers.
  mmio_.reg(MEMBW_PORTS_CTRL).SetWriteCallback(&WriteDisallowed);
  mmio_.reg(MEMBW_TIMER).SetWriteCallback(&WriteDisallowed);

  ram_metrics::Device::SyncClient client{std::move(ddk_.FidlClient())};

  // Invalid cycles (too low).
  {
    ram_metrics::BandwidthMeasurementConfig config = {(200), {1, 0, 0, 0, 0, 0}};
    auto info = client.MeasureBandwidth(config);
    ASSERT_TRUE(info.ok());
    ASSERT_TRUE(info->result.is_err());
    EXPECT_EQ(info->result.err(), ZX_ERR_INVALID_ARGS);
  }

  // Invalid cycles (too high).
  {
    ram_metrics::BandwidthMeasurementConfig config = {(0x100000000ull), {1, 0, 0, 0, 0, 0}};
    auto info = client.MeasureBandwidth(config);
    ASSERT_TRUE(info.ok());
    ASSERT_TRUE(info->result.is_err());
    EXPECT_EQ(info->result.err(), ZX_ERR_INVALID_ARGS);
  }

  // Invalid channel (above channel 3).
  {
    ram_metrics::BandwidthMeasurementConfig config = {(1024 * 1024 * 10), {0, 0, 0, 0, 1}};
    auto info = client.MeasureBandwidth(config);
    ASSERT_TRUE(info.ok());
    ASSERT_TRUE(info->result.is_err());
    EXPECT_EQ(info->result.err(), ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(AmlRamDeviceTest, ValidRequest) {
  // Peform a request for 3 channels. The harness provides the data that should be
  // read via mmio and verifies that the control registers are accessed in the
  // right sequence.
  constexpr uint32_t kCyclesToMeasure = (1024 * 1024 * 10u);
  constexpr uint32_t kControlStart = DMC_QOS_ENABLE_CTRL | 0b0111;
  constexpr uint32_t kControlStop = DMC_QOS_CLEAR_CTRL | 0b1111;
  constexpr uint32_t kFreq = 0x4 | (0x1 << 10);  // F=24000000 (M=4, N=1, OD=0, OD1=0)

  // Note that the cycles are to be interpreted as shifted 4 bits.
  constexpr uint32_t kReadCycles[] = {0x125001, 0x124002, 0x123003, 0x0};

  ram_metrics::BandwidthMeasurementConfig config = {kCyclesToMeasure, {4, 2, 1, 0, 0, 0}};

  // |step| helps track of the expected sequence of reads and writes.
  std::atomic<int> step = 0;

  mmio_.reg(MEMBW_TIMER).SetWriteCallback([expect = kCyclesToMeasure, &step](size_t value) {
    EXPECT_EQ(step, 0, "unexpected: 0x%lx", value);
    EXPECT_EQ(value, expect, "0: got write of 0x%lx", value);
    ++step;
  });

  mmio_.reg(MEMBW_PORTS_CTRL)
      .SetWriteCallback([this, start = kControlStart, stop = kControlStop, &step](size_t value) {
        if (step == 1) {
          EXPECT_EQ(value, start, "1: got write of 0x%lx", value);
          InjectInterrupt();
          ++step;
        } else if (step == 4) {
          EXPECT_EQ(value, stop, "4: got write of 0x%lx", value);
          ++step;
        } else {
          EXPECT_TRUE(false, "unexpected: 0x%lx", value);
        }
      });

  mmio_.reg(MEMBW_PLL_CNTL).SetReadCallback([value = kFreq]() { return value; });

  // Note that reading from MEMBW_PORTS_CTRL by default returns 0
  // and that makes the operation succeed.

  mmio_.reg(MEMBW_C0_GRANT_CNT).SetReadCallback([&step, value = kReadCycles[0]]() {
    EXPECT_EQ(step, 2);
    // Value of channel 0 cycles granted.
    return value;
  });

  mmio_.reg(MEMBW_C1_GRANT_CNT).SetReadCallback([&step, value = kReadCycles[1]]() {
    EXPECT_EQ(step, 2);
    // Value of channel 1 cycles granted.
    return value;
  });

  mmio_.reg(MEMBW_C2_GRANT_CNT).SetReadCallback([&step, value = kReadCycles[2]]() {
    EXPECT_EQ(step, 2);
    // Value of channel 2 cycles granted.
    return value;
  });

  mmio_.reg(MEMBW_C3_GRANT_CNT).SetReadCallback([&step, value = kReadCycles[3]]() {
    EXPECT_EQ(step, 2);
    ++step;
    // Value of channel 3 cycles granted.
    return value;
  });

  mmio_.reg(MEMBW_ALL_GRANT_CNT)
      .SetReadCallback(
          [&step, value = kReadCycles[0] + kReadCycles[1] + kReadCycles[2] + kReadCycles[3]]() {
            EXPECT_EQ(step, 3);
            ++step;
            // Value of all cycles granted.
            return value;
          });

  ram_metrics::Device::SyncClient client{std::move(ddk_.FidlClient())};
  auto info = client.MeasureBandwidth(config);
  ASSERT_TRUE(info.ok());
  ASSERT_FALSE(info->result.is_err());

  // Check All mmio reads and writes happened.
  EXPECT_EQ(step, 5);

  EXPECT_GT(info->result.response().info.timestamp, 0u);
  EXPECT_EQ(info->result.response().info.frequency, 24000000u);
  EXPECT_EQ(info->result.response().info.bytes_per_cycle, 16u);

  // Check FIDL result makes sense. AML hw does not support read or write only counters.
  int ix = 0;
  for (auto& c : info->result.response().info.channels) {
    if (ix < 4) {
      EXPECT_EQ(c.readwrite_cycles, kReadCycles[ix]);
    } else {
      EXPECT_EQ(c.readwrite_cycles, 0u);
    }
    EXPECT_EQ(c.write_cycles, 0u);
    EXPECT_EQ(c.read_cycles, 0u);
    ++ix;
  }
  EXPECT_EQ(info->result.response().info.total.readwrite_cycles,
            kReadCycles[0] + kReadCycles[1] + kReadCycles[2] + kReadCycles[3]);
}

}  // namespace amlogic_ram

// We replace this method to allow the FakePDev::PDevGetMmio() to work
// with the driver unmodified. The real implementation tries to map a VMO that
// we can't properly fake at the moment.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio, uint32_t cache_policy) {
  auto* src = reinterpret_cast<amlogic_ram::FakeMmio*>(pdev_mmio.offset);
  mmio->emplace(src->mmio());
  return ZX_OK;
}
