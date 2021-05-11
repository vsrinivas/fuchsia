// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vs680-thermal.h"

#include <fuchsia/hardware/clock/cpp/banjo-mock.h>
#include <fuchsia/hardware/power/cpp/banjo-mock.h>
#include <lib/fake_ddk/fidl-helper.h>
#include <lib/mmio-ptr/fake.h>
#include <lib/zx/clock.h>

#include <memory>

#include <zxtest/zxtest.h>

namespace {

bool FloatNear(float a, float b) { return std::abs(a - b) < 0.001f; }

}  // namespace

namespace thermal {

using ThermalClient = fidl::WireSyncClient<fuchsia_hardware_thermal::Device>;

class Vs680ThermalTest : public zxtest::Test {
 public:
  Vs680ThermalTest() {}

  void SetUp() override {
    ddk::MmioBuffer mmio({FakeMmioPtr(registers_), 0, sizeof(registers_), ZX_HANDLE_INVALID});

    ASSERT_OK(zx::interrupt::create({}, 0, ZX_INTERRUPT_VIRTUAL, &interrupt_));
    zx::interrupt dut_interrupt;
    ASSERT_OK(interrupt_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dut_interrupt));

    dut_.reset(new Vs680Thermal(nullptr, std::move(mmio), std::move(dut_interrupt),
                                clock_.GetProto(), power_.GetProto(), zx::msec(1)));

    ASSERT_OK(messenger_.SetMessageOp(
        dut_.get(), [](void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) -> zx_status_t {
          return static_cast<Vs680Thermal*>(ctx)->ddk_device_proto_.message(ctx, msg, txn);
        }));

    power_.ExpectRegisterPowerDomain(ZX_OK, 800'000, 800'000);

    clock_.ExpectSetRate(ZX_OK, 1'800'000'000);
    power_.ExpectRequestVoltage(ZX_OK, 800'000, 800'000);
    ASSERT_OK(dut_->Init());

    // Init() started the interrupt thread, so DdkRelease must be called before destruction of the
    // Vs680Thermal object.
  }

  void TearDown() override {
    dut_->DdkRelease();
    dut_.release();
  }

 protected:
  uint32_t registers_[0x180 / 4];
  zx::interrupt interrupt_;
  ddk::MockClock clock_;
  ddk::MockPower power_;
  std::unique_ptr<Vs680Thermal> dut_;
  fake_ddk::FidlMessenger messenger_;
};

TEST_F(Vs680ThermalTest, GetTemperature) {
  ThermalClient client(std::move(messenger_.local()));

  auto result = client.GetTemperatureCelsius();
  EXPECT_OK(result->status);
  EXPECT_TRUE(FloatNear(result->temp, 0.0f));

  registers_[0x108 / 4] = 311;
  EXPECT_OK(interrupt_.trigger(0, zx::clock::get_monotonic()));

  // Rather than trying to synchronize with the polling thread, just loop until we get the value we
  // expect.
  auto temp = result->temp;
  while (!FloatNear(temp, 37.812f)) {
    auto result = client.GetTemperatureCelsius();
    EXPECT_OK(result->status);
    temp = result->temp;
  }

  registers_[0x108 / 4] = 358;
  EXPECT_OK(interrupt_.trigger(0, zx::clock::get_monotonic()));

  temp = result->temp;
  while (!FloatNear(temp, 48.576f)) {
    auto result = client.GetTemperatureCelsius();
    EXPECT_OK(result->status);
    temp = result->temp;
  }
}

TEST_F(Vs680ThermalTest, OperatingPoints) {
  ThermalClient client(std::move(messenger_.local()));

  auto get_result = client.GetDvfsOperatingPoint(PowerDomain::kBigClusterPowerDomain);
  EXPECT_OK(get_result->status);
  EXPECT_EQ(get_result->op_idx, 0);

  clock_.ExpectSetRate(ZX_OK, 1'800'000'000);
  power_.ExpectRequestVoltage(ZX_OK, 800'000, 800'000);
  auto set_result = client.SetDvfsOperatingPoint(0, PowerDomain::kBigClusterPowerDomain);
  EXPECT_OK(set_result->status);

  ASSERT_NO_FATAL_FAILURES(clock_.VerifyAndClear());
  ASSERT_NO_FATAL_FAILURES(power_.VerifyAndClear());
}

}  // namespace thermal
