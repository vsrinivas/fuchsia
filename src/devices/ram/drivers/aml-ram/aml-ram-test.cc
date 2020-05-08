// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-ram.h"

#include <lib/device-protocol/pdev.h>
#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fake_ddk/fidl-helper.h>

#include <atomic>
#include <memory>

#include <ddktl/device.h>
#include <ddktl/protocol/platform/bus.h>
#include <ddktl/protocol/platform/device.h>
#include <fake-mmio-reg/fake-mmio-reg.h>

namespace amlogic_ram {

constexpr size_t kRegSize = 0x01000 / sizeof(uint32_t);

class FakePDev : public ddk::PDevProtocol<FakePDev, ddk::base_protocol> {
 public:
  FakePDev() : proto_({&pdev_protocol_ops_, this}) {
    regs_ = std::make_unique<ddk_fake::FakeMmioReg[]>(kRegSize);
    mmio_ = std::make_unique<ddk_fake::FakeMmioRegRegion>(regs_.get(), sizeof(uint32_t), kRegSize);
  }

  zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio) {
    EXPECT_EQ(index, 0);
    out_mmio->offset = reinterpret_cast<size_t>(this);
    return ZX_OK;
  }

  zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx::interrupt* out_irq) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetBti(uint32_t index, zx::bti* out_bti) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetSmc(uint32_t index, zx::resource* out_resource) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

  zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info) { return ZX_ERR_NOT_SUPPORTED; }

  const pdev_protocol_t* proto() const { return &proto_; }
  ddk::MmioBuffer mmio() { return ddk::MmioBuffer(mmio_->GetMmioBuffer()); }

  ddk_fake::FakeMmioReg& reg(size_t ix) {
    // AML registers are in virtual address units.
    return regs_[ix >> 2];
  }

 private:
  pdev_protocol_t proto_;
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
    fbl::Array<fake_ddk::ProtocolEntry> protocols(new fake_ddk::ProtocolEntry[1], 1);
    protocols[0] = {ZX_PROTOCOL_PDEV, {pdev_.proto()->ops, pdev_.proto()->ctx}};
    ddk_.SetProtocols(std::move(protocols));

    EXPECT_OK(amlogic_ram::AmlRam::Create(nullptr, fake_ddk::FakeParent()));
  }

  void TearDown() override {
    auto device = static_cast<amlogic_ram::AmlRam*>(ddk_.args().ctx);
    device->DdkAsyncRemove();
    EXPECT_TRUE(ddk_.Ok());
  }

 protected:
  FakePDev pdev_;
  Ddk ddk_;
};

void WriteDisallowed(uint64_t value) { EXPECT_TRUE(false, "got register write of 0x%lx", value); }

TEST_F(AmlRamDeviceTest, InitDoesNothing) {
  // By itself the driver does not write to registers.
  // The fixture's TearDown() test also the lifecycle bits.
  pdev_.reg(MEMBW_PORTS_CTRL).SetWriteCallback(&WriteDisallowed);
  pdev_.reg(MEMBW_TIMER).SetWriteCallback(&WriteDisallowed);
}

TEST_F(AmlRamDeviceTest, MalformedRequests) {
  // An invalid request does not write to registers.
  pdev_.reg(MEMBW_PORTS_CTRL).SetWriteCallback(&WriteDisallowed);
  pdev_.reg(MEMBW_TIMER).SetWriteCallback(&WriteDisallowed);

  ram_metrics::BandwidthMeasurementConfig config;
  ram_metrics::Device::SyncClient client{std::move(ddk_.FidlClient())};

  // Invalid cycles (too low).
  config = {(200), {1, 0, 0, 0, 0, 0}};
  auto info = client.MeasureBandwidth(config);
  ASSERT_TRUE(info.ok());
  ASSERT_TRUE(info->result.is_err());
  EXPECT_EQ(info->result.err(), ZX_ERR_INVALID_ARGS);

  // Invalid channel (above channel 3).
  config = {(1024 * 1024 * 10), {0, 0, 0, 0, 1}};
  info = client.MeasureBandwidth(config);
  ASSERT_TRUE(info.ok());
  ASSERT_TRUE(info->result.is_err());
  EXPECT_EQ(info->result.err(), ZX_ERR_INVALID_ARGS);
}

#if 0
// This test is disabled until we have fxb/51461 fixed.

TEST_F(AmlRamDeviceTest, ValidRequest) {
  constexpr uint32_t kCyclesToMeasure = (1024 * 1024 * 10u);
  constexpr uint32_t kControlStart = DMC_QOS_ENABLE_CTRL | 0b0111;
  constexpr uint32_t kControlStop = DMC_QOS_CLEAR_CTRL | 0b0111;
  constexpr uint32_t kReadCycles[] = {0x125001, 0x124002, 0x123003, 0x0};

  ram_metrics::BandwidthMeasurementConfig config = {kCyclesToMeasure, {4, 2, 1, 0, 0, 0}};

  // |step| helps track of the expected sequence of reads and writes.
  std::atomic<int> step = 0;

  pdev_.reg(MEMBW_TIMER).SetWriteCallback([expect = kCyclesToMeasure, &step](size_t value) {
    EXPECT_EQ(step, 0, "unexpected: 0x%lx", value);
    ++step;
    EXPECT_EQ(value, expect, "got write of 0x%lx", value);
  });

  pdev_.reg(MEMBW_PORTS_CTRL)
      .SetWriteCallback([start = kControlStart, stop = kControlStop, &step](size_t value) {
        if (step == 1) {
          EXPECT_EQ(value, start, "0: got write of 0x%lx", value);
          ++step;
        } else if (step == 3) {
          EXPECT_EQ(value, stop, "2: got write of 0x%lx", value);
          ++step;
        } else {
          EXPECT_TRUE(false, "unexpected: 0x%lx", value);
        }
      });

  pdev_.reg(MEMBW_C0_GRANT_CNT).SetReadCallback([&step, value = kReadCycles[0]]() {
    EXPECT_EQ(step, 2);
    // Value of channel 0 cycles granted.
    return value;
  });

  pdev_.reg(MEMBW_C1_GRANT_CNT).SetReadCallback([&step, value = kReadCycles[1]]() {
    EXPECT_EQ(step, 2);
    // Value of channel 1 cycles granted.
    return value;
  });

  pdev_.reg(MEMBW_C2_GRANT_CNT).SetReadCallback([&step, value = kReadCycles[2]]() {
    EXPECT_EQ(step, 2);
    // Value of channel 2 cycles granted.
    return value;
  });

  pdev_.reg(MEMBW_C2_GRANT_CNT).SetReadCallback([&step, value = kReadCycles[3]]() {
    EXPECT_EQ(step, 2);
    ++step;
    // Value of channel 3 cycles granted.
    return value;
  });

  ram_metrics::Device::SyncClient client{std::move(ddk_.FidlClient())};
  auto info = client.MeasureBandwidth(config);
  ASSERT_TRUE(info.ok());
  ASSERT_FALSE(info->result.is_err());

  // All reads and writes happened.
  EXPECT_EQ(step, 4);
}
#endif

}  // namespace amlogic_ram

// We replace this method to allow the FakePDev::PDevGetMmio() to work
// with the driver unmodified. The real implementation tries to map a VMO that
// we can't properly fake at the moment.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio) {
  auto* src = reinterpret_cast<amlogic_ram::FakePDev*>(pdev_mmio.offset);
  mmio->emplace(src->mmio());
  return ZX_OK;
}
