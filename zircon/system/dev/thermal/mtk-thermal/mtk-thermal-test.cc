// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-thermal.h"

#include <lib/mock-function/mock-function.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <memory>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/mt8167/mt8167-hw.h>

namespace {

constexpr size_t kThermalRegCount = MT8167_THERMAL_SIZE / sizeof(uint32_t);
constexpr size_t kPllRegCount = MT8167_AP_MIXED_SYS_SIZE / sizeof(uint32_t);
constexpr size_t kPmicWrapRegCount = MT8167_PMIC_WRAP_SIZE / sizeof(uint32_t);
constexpr size_t kInfraCfgRegCount = MT8167_INFRACFG_SIZE / sizeof(uint32_t);

template <typename T>
ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get().addr()];
}

template <typename T>
ddk_mock::MockMmioReg& GetMockReg(int index, ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get(index).addr()];
}

constexpr fuchsia_hardware_thermal_ThermalTemperatureInfo TripPoint(float temp, uint16_t opp) {
  fuchsia_hardware_thermal_ThermalTemperatureInfo trip = {};
  trip.up_temp_celsius = temp + 2.0f;
  trip.down_temp_celsius = temp - 2.0f;
  trip.big_cluster_dvfs_opp = opp;
  return trip;
}

}  // namespace

namespace thermal {

class MtkThermalTest : public MtkThermal {
 public:
  MtkThermalTest(mmio_buffer_t dummy_mmio, const ddk::CompositeProtocolClient& composite,
                 const ddk::PDevProtocolClient& pdev,
                 const fuchsia_hardware_thermal_ThermalDeviceInfo& thermal_info, zx::port port,
                 TempCalibration0 cal0_fuse, TempCalibration1 cal1_fuse, TempCalibration2 cal2_fuse,
                 zx::port main_port, zx::port thread_port)
      : MtkThermal(nullptr, ddk::MmioBuffer(dummy_mmio), ddk::MmioBuffer(dummy_mmio),
                   ddk::MmioBuffer(dummy_mmio), ddk::MmioBuffer(dummy_mmio), composite, pdev,
                   thermal_info, std::move(port), zx::interrupt(), cal0_fuse, cal1_fuse, cal2_fuse),
        mock_thermal_regs_(thermal_reg_array_, sizeof(uint32_t), MT8167_THERMAL_SIZE),
        mock_pll_regs_(pll_reg_array_, sizeof(uint32_t), MT8167_AP_MIXED_SYS_SIZE),
        mock_pmic_wrap_regs_(pmic_wrap_reg_array_, sizeof(uint32_t), MT8167_PMIC_WRAP_SIZE),
        mock_infracfg_regs_(infracfg_reg_array_, sizeof(uint32_t), MT8167_INFRACFG_SIZE),
        main_port_(std::move(main_port)),
        thread_port_(std::move(thread_port)) {
    mmio_ = ddk::MmioBuffer(mock_thermal_regs_.GetMmioBuffer());
    pll_mmio_ = ddk::MmioBuffer(mock_pll_regs_.GetMmioBuffer());
    pmic_mmio_ = ddk::MmioBuffer(mock_pmic_wrap_regs_.GetMmioBuffer());
    infracfg_mmio_ = ddk::MmioBuffer(mock_infracfg_regs_.GetMmioBuffer());
  }

  static bool Create(const fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info, zx::port port,
                     std::unique_ptr<MtkThermalTest>* test) {
    mmio_buffer_t dummy_mmio;
    dummy_mmio.vaddr = &dummy_mmio;
    dummy_mmio.size = sizeof(dummy_mmio);

    TempCalibration0 cal0_fuse;
    cal0_fuse.set_reg_value(kCal0Fuse);

    TempCalibration1 cal1_fuse;
    cal1_fuse.set_reg_value(kCal1Fuse);

    TempCalibration2 cal2_fuse;
    cal2_fuse.set_reg_value(kCal2Fuse);

    zx::port main_port;
    if (zx::port::create(0, &main_port) != ZX_OK) {
      return false;
    }

    zx::port thread_port;
    if (main_port.duplicate(ZX_RIGHT_SAME_RIGHTS, &thread_port) != ZX_OK) {
      return false;
    }

    fbl::AllocChecker ac;
    test->reset(new (&ac) MtkThermalTest(dummy_mmio, ddk::CompositeProtocolClient(),
                                         ddk::PDevProtocolClient(), thermal_info, std::move(port),
                                         cal0_fuse, cal1_fuse, cal2_fuse, std::move(main_port),
                                         std::move(thread_port)));
    return ac.check();
  }

  ddk_mock::MockMmioRegRegion& thermal_regs() { return mock_thermal_regs_; }
  ddk_mock::MockMmioRegRegion& pll_regs() { return mock_pll_regs_; }
  ddk_mock::MockMmioRegRegion& pmic_wrap_regs() { return mock_pmic_wrap_regs_; }
  ddk_mock::MockMmioRegRegion& infracfg_regs() { return mock_infracfg_regs_; }

  mock_function::MockFunction<void, uint16_t, uint32_t>& mock_PmicWrite() {
    return mock_pmic_write_;
  }
  mock_function::MockFunction<float>& mock_ReadTemperatureSensors() {
    return mock_get_temperature_;
  }
  mock_function::MockFunction<zx_status_t, uint16_t>& mock_SetDvfsOpp() {
    return mock_set_dvfs_opp_;
  }
  mock_function::MockFunction<zx_status_t, size_t>& mock_SetTripPoint() {
    return mock_set_trip_point_;
  }

  void VerifyAll() {
    for (size_t i = 0; i < kThermalRegCount; i++) {
      mock_thermal_regs_[i].VerifyAndClear();
    }

    for (size_t i = 0; i < kPllRegCount; i++) {
      mock_pll_regs_[i].VerifyAndClear();
    }

    for (size_t i = 0; i < kPmicWrapRegCount; i++) {
      mock_pmic_wrap_regs_[i].VerifyAndClear();
    }

    mock_pmic_write_.VerifyAndClear();
    mock_get_temperature_.VerifyAndClear();
    mock_set_dvfs_opp_.VerifyAndClear();
  }

  void PmicWrite(uint16_t data, uint32_t addr) override {
    if (mock_pmic_write_.HasExpectations()) {
      mock_pmic_write_.Call(data, addr);
    } else {
      MtkThermal::PmicWrite(data, addr);
    }
  }

  float ReadTemperatureSensors() override {
    if (mock_get_temperature_.HasExpectations()) {
      return mock_get_temperature_.Call();
    } else {
      return MtkThermal::ReadTemperatureSensors();
    }
  }

  zx_status_t SetDvfsOpp(uint16_t op_idx) override {
    if (mock_set_dvfs_opp_.HasExpectations()) {
      return mock_set_dvfs_opp_.Call(op_idx);
    } else {
      return MtkThermal::SetDvfsOpp(op_idx);
    }
  }

  zx_status_t SetTripPoint(size_t trip_pt) override {
    if (mock_set_trip_point_.HasExpectations()) {
      return mock_set_trip_point_.Call(trip_pt);
    } else {
      return MtkThermal::SetTripPoint(trip_pt);
    }
  }

  // Trigger count interrupts without waiting for them to be handled.
  zx_status_t TriggerInterrupts(uint32_t count) {
    zx_port_packet_t packet;
    packet.key = kPacketKeyInterrupt;
    packet.type = ZX_PKT_TYPE_USER;
    packet.user.u32[0] = count;
    return main_port_.queue(&packet);
  }

  // Wait for the thread to finish processing interrupts and join.
  zx_status_t StopThread() override {
    zx_port_packet_t packet;
    packet.key = kPacketKeyStopThread;
    packet.type = ZX_PKT_TYPE_USER;
    zx_status_t status = main_port_.queue(&packet);

    JoinThread();
    return status;
  }

 private:
  // These were taken from a real device.
  static constexpr uint32_t kCal0Fuse = 0x29389d67;
  static constexpr uint32_t kCal1Fuse = 0x805f84a9;
  static constexpr uint32_t kCal2Fuse = 0x4eaad600;

  static constexpr uint64_t kPacketKeyInterrupt = 0;
  static constexpr uint64_t kPacketKeyStopThread = 1;

  zx_status_t WaitForInterrupt() override {
    if (interrupt_count_ > 0) {
      interrupt_count_--;
      return ZX_OK;
    }

    zx_port_packet_t packet;
    zx_status_t status = thread_port_.wait(zx::deadline_after(zx::duration::infinite()), &packet);
    if (status != ZX_OK) {
      return ZX_ERR_CANCELED;
    }

    if (packet.type == ZX_PKT_TYPE_USER && packet.key == kPacketKeyInterrupt) {
      interrupt_count_ = packet.user.u32[0] - 1;
      return ZX_OK;
    }

    return ZX_ERR_CANCELED;
  }

  ddk_mock::MockMmioReg thermal_reg_array_[kThermalRegCount];
  ddk_mock::MockMmioReg pll_reg_array_[kPllRegCount];
  ddk_mock::MockMmioReg pmic_wrap_reg_array_[kPmicWrapRegCount];
  ddk_mock::MockMmioReg infracfg_reg_array_[kInfraCfgRegCount];

  ddk_mock::MockMmioRegRegion mock_thermal_regs_;
  ddk_mock::MockMmioRegRegion mock_pll_regs_;
  ddk_mock::MockMmioRegRegion mock_pmic_wrap_regs_;
  ddk_mock::MockMmioRegRegion mock_infracfg_regs_;

  mock_function::MockFunction<void, uint16_t, uint32_t> mock_pmic_write_;
  mock_function::MockFunction<float> mock_get_temperature_;
  mock_function::MockFunction<zx_status_t, uint16_t> mock_set_dvfs_opp_;
  mock_function::MockFunction<zx_status_t, size_t> mock_set_trip_point_;

  uint32_t interrupt_count_ = 0;
  zx::port main_port_;
  zx::port thread_port_;
};

TEST(ThermalTest, TripPoints) {
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info;
  thermal_info.num_trip_points = 3;
  thermal_info.critical_temp_celsius = 50.0f;
  thermal_info.trip_point_info[0] = TripPoint(20.0f, 2);
  thermal_info.trip_point_info[1] = TripPoint(30.0f, 1);
  thermal_info.trip_point_info[2] = TripPoint(40.0f, 0);

  std::unique_ptr<MtkThermalTest> test;
  ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

  test->mock_SetDvfsOpp().ExpectNoCall();

  uint32_t up_int = TempMonIntStatus::Get().FromValue(0).set_hot_0(1).reg_value();
  uint32_t down_int = TempMonIntStatus::Get().FromValue(0).set_cold_0(1).reg_value();

  test->mock_ReadTemperatureSensors().ExpectCall(20.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);

  test->mock_ReadTemperatureSensors().ExpectCall(35.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 1);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

  test->mock_ReadTemperatureSensors().ExpectCall(45.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

  test->mock_ReadTemperatureSensors().ExpectCall(25.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 1);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

  test->mock_ReadTemperatureSensors().ExpectCall(15.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

  EXPECT_OK(test->StartThread());

  EXPECT_OK(test->TriggerInterrupts(4));
  EXPECT_OK(test->StopThread());
  test->VerifyAll();
}

TEST(ThermalTest, CriticalTemperature) {
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info;
  thermal_info.num_trip_points = 3;
  thermal_info.critical_temp_celsius = 50.0f;
  thermal_info.trip_point_info[0] = TripPoint(20.0f, 2);
  thermal_info.trip_point_info[1] = TripPoint(30.0f, 1);
  thermal_info.trip_point_info[2] = TripPoint(40.0f, 0);

  std::unique_ptr<MtkThermalTest> test;
  ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

  uint32_t critical_int = TempMonIntStatus::Get().FromValue(0).set_stage_3(1).reg_value();

  test->mock_ReadTemperatureSensors().ExpectCall(20.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);

  test->mock_ReadTemperatureSensors().ExpectCall(55.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);
  test->mock_SetDvfsOpp().ExpectCall(ZX_OK, 0);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(critical_int);

  EXPECT_OK(test->StartThread());

  EXPECT_OK(test->TriggerInterrupts(1));
  EXPECT_OK(test->StopThread());
  test->VerifyAll();
}

TEST(ThermalTest, InitialTripPoint) {
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info;
  thermal_info.num_trip_points = 3;
  thermal_info.critical_temp_celsius = 50.0f;
  thermal_info.trip_point_info[0] = TripPoint(20.0f, 2);
  thermal_info.trip_point_info[1] = TripPoint(30.0f, 1);
  thermal_info.trip_point_info[2] = TripPoint(40.0f, 0);

  std::unique_ptr<MtkThermalTest> test;
  ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

  test->mock_ReadTemperatureSensors().ExpectCall(45.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);

  EXPECT_OK(test->StartThread());

  EXPECT_OK(test->StopThread());
  test->VerifyAll();
}

TEST(ThermalTest, TripPointJumpMultiple) {
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info;
  thermal_info.num_trip_points = 5;
  thermal_info.critical_temp_celsius = 100.0f;
  thermal_info.trip_point_info[0] = TripPoint(20.0f, 4);
  thermal_info.trip_point_info[1] = TripPoint(30.0f, 3);
  thermal_info.trip_point_info[2] = TripPoint(40.0f, 2);
  thermal_info.trip_point_info[3] = TripPoint(50.0f, 1);
  thermal_info.trip_point_info[4] = TripPoint(60.0f, 0);

  std::unique_ptr<MtkThermalTest> test;
  ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

  uint32_t up_int = TempMonIntStatus::Get().FromValue(0).set_hot_0(1).reg_value();
  uint32_t down_int = TempMonIntStatus::Get().FromValue(0).set_cold_0(1).reg_value();

  test->mock_ReadTemperatureSensors().ExpectCall(20.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);

  test->mock_ReadTemperatureSensors().ExpectCall(45.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

  test->mock_ReadTemperatureSensors().ExpectCall(65.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 4);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

  test->mock_ReadTemperatureSensors().ExpectCall(15.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

  test->mock_ReadTemperatureSensors().ExpectCall(55.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 3);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

  test->mock_ReadTemperatureSensors().ExpectCall(25.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 1);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

  test->mock_ReadTemperatureSensors().ExpectCall(65.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 4);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

  test->mock_ReadTemperatureSensors().ExpectCall(35.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

  test->mock_ReadTemperatureSensors().ExpectCall(15.0f);
  test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);
  GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

  EXPECT_OK(test->StartThread());

  EXPECT_OK(test->TriggerInterrupts(8));
  EXPECT_OK(test->StopThread());
  test->VerifyAll();
}

TEST(ThermalTest, SetTripPoint) {
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info;
  thermal_info.num_trip_points = 3;
  thermal_info.trip_point_info[0] = TripPoint(20.0f, 2);
  thermal_info.trip_point_info[1] = TripPoint(30.0f, 1);
  thermal_info.trip_point_info[2] = TripPoint(40.0f, 0);

  zx::port port;
  ASSERT_OK(zx::port::create(0, &port));

  std::unique_ptr<MtkThermalTest> test;
  ASSERT_TRUE(MtkThermalTest::Create(thermal_info, std::move(port), &test));

  ASSERT_OK(test->GetPort(&port));

  GetMockReg<TempHotThreshold>(test->thermal_regs()).ExpectWrite();
  GetMockReg<TempColdThreshold>(test->thermal_regs()).ExpectWrite();

  test->SetTripPoint(0);

  zx_port_packet_t packet;
  ASSERT_OK(port.wait(zx::deadline_after(zx::duration::infinite()), &packet));
  EXPECT_EQ(ZX_PKT_TYPE_USER, packet.type);
  EXPECT_EQ(0, packet.key);

  EXPECT_NE(0, GetMockReg<TempHotThreshold>(test->thermal_regs()).Read());
  EXPECT_EQ(0xfff, GetMockReg<TempColdThreshold>(test->thermal_regs()).Read());

  ASSERT_NO_FATAL_FAILURES(test->VerifyAll());

  GetMockReg<TempHotThreshold>(test->thermal_regs()).ExpectWrite();
  GetMockReg<TempColdThreshold>(test->thermal_regs()).ExpectWrite();

  test->SetTripPoint(1);

  ASSERT_OK(port.wait(zx::deadline_after(zx::duration::infinite()), &packet));
  EXPECT_EQ(ZX_PKT_TYPE_USER, packet.type);
  EXPECT_EQ(1, packet.key);

  EXPECT_NE(0, GetMockReg<TempHotThreshold>(test->thermal_regs()).Read());
  EXPECT_NE(0, GetMockReg<TempColdThreshold>(test->thermal_regs()).Read());

  ASSERT_NO_FATAL_FAILURES(test->VerifyAll());

  GetMockReg<TempHotThreshold>(test->thermal_regs()).ExpectWrite();
  GetMockReg<TempColdThreshold>(test->thermal_regs()).ExpectWrite();

  test->SetTripPoint(2);

  ASSERT_OK(port.wait(zx::deadline_after(zx::duration::infinite()), &packet));
  EXPECT_EQ(ZX_PKT_TYPE_USER, packet.type);
  EXPECT_EQ(2, packet.key);

  EXPECT_EQ(0, GetMockReg<TempHotThreshold>(test->thermal_regs()).Read());
  EXPECT_NE(0, GetMockReg<TempColdThreshold>(test->thermal_regs()).Read());

  test->VerifyAll();
}

TEST(ThermalTest, DvfsOpp) {
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info;
  thermal_info.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN].count = 3;
  thermal_info.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN].opp[0] = {
      598'000'000, 1'150'000};
  thermal_info.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN].opp[1] = {
      747'500'000, 1'150'000};
  thermal_info.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN].opp[2] = {
      1'040'000'000, 1'200'000};

  std::unique_ptr<MtkThermalTest> test;
  ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

  auto voltage_to_step = [](uint32_t volt_uv) -> uint16_t {
    return static_cast<uint16_t>((volt_uv - 700'000) / 6250);
  };

  auto frequency_to_reg_value = [](uint32_t freq_hz) -> uint32_t {
    uint64_t pcw = (static_cast<uint64_t>(freq_hz) << 14) / 26'000'000;
    return (1 << 31) | static_cast<uint32_t>(pcw);
  };

  test->mock_PmicWrite().ExpectCall(voltage_to_step(1'150'000), 0x110);

  GetMockReg<ArmPllCon1>(test->pll_regs())
      .ExpectRead(frequency_to_reg_value(598'000'000))
      .ExpectWrite(frequency_to_reg_value(747'500'000));

  EXPECT_OK(test->SetDvfsOpp(1));
  EXPECT_EQ(1, test->get_dvfs_opp());

  ASSERT_NO_FATAL_FAILURES(test->VerifyAll());

  test->mock_PmicWrite().ExpectCall(voltage_to_step(1'200'000), 0x110);

  GetMockReg<ArmPllCon1>(test->pll_regs())
      .ExpectRead(frequency_to_reg_value(747'500'000))
      .ExpectWrite(frequency_to_reg_value(1'040'000'000));

  EXPECT_OK(test->SetDvfsOpp(2));
  EXPECT_EQ(2, test->get_dvfs_opp());

  ASSERT_NO_FATAL_FAILURES(test->VerifyAll());

  test->mock_PmicWrite().ExpectCall(voltage_to_step(1150000), 0x110);

  GetMockReg<ArmPllCon1>(test->pll_regs())
      .ExpectRead(frequency_to_reg_value(1'040'000'000))
      .ExpectWrite(frequency_to_reg_value(598'000'000));

  EXPECT_OK(test->SetDvfsOpp(0));
  EXPECT_EQ(0, test->get_dvfs_opp());

  test->VerifyAll();
}

TEST(ThermalTest, DvfsOppVoltageRange) {
  fuchsia_hardware_thermal_ThermalDeviceInfo thermal_info;
  thermal_info.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN].count = 1;

  std::unique_ptr<MtkThermalTest> test;

  thermal_info.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN].opp[0] = {
      1'000'000'000, 100'000};
  ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));
  EXPECT_NE(ZX_OK, test->SetDvfsOpp(0));

  thermal_info.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN].opp[0] = {
      1'000'000'000, 1'500'000};
  ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));
  EXPECT_NE(ZX_OK, test->SetDvfsOpp(0));

  thermal_info.opps[fuchsia_hardware_thermal_PowerDomain_BIG_CLUSTER_POWER_DOMAIN].opp[0] = {
      1'000'000'000, 1'151'000};
  ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));
  EXPECT_NE(ZX_OK, test->SetDvfsOpp(0));
}

TEST(ThermalTest, PmicWrite) {
  std::unique_ptr<MtkThermalTest> test;
  ASSERT_TRUE(MtkThermalTest::Create({}, zx::port(), &test));

  GetMockReg<PmicReadData>(test->pmic_wrap_regs())
      .ExpectRead(0x00060000)
      .ExpectRead(0x00060000)
      .ExpectRead(0x00060000)
      .ExpectRead(0x00000000);

  GetMockReg<PmicCmd>(test->pmic_wrap_regs()).ExpectWrite(0xce8761df);

  test->PmicWrite(0x61df, 0x4e87);
  ASSERT_NO_FATAL_FAILURES(test->VerifyAll());

  GetMockReg<PmicReadData>(test->pmic_wrap_regs()).ExpectRead(0x00060000).ExpectRead(0x00000000);

  GetMockReg<PmicCmd>(test->pmic_wrap_regs()).ExpectWrite(0xf374504f);

  test->PmicWrite(0x504f, 0x7374);
  test->VerifyAll();
}

}  // namespace thermal
