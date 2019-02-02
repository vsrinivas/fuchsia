// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mtk-thermal.h"

#include <tuple>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <lib/sync/completion.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/mt8167/mt8167-hw.h>
#include <zircon/syscalls/port.h>
#include <zircon/thread_annotations.h>
#include <zxtest/zxtest.h>

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

constexpr uint32_t CToKTenths(uint32_t temp_c) {
    constexpr uint32_t kKelvinOffset = 2732;  // Units: 0.1 degrees C
    return (temp_c * 10) + kKelvinOffset;
}

constexpr thermal_temperature_info_t TripPoint(uint32_t temp, int32_t opp) {
    thermal_temperature_info_t trip = {};
    trip.up_temp = CToKTenths(temp + 2);
    trip.down_temp = CToKTenths(temp - 2);
    trip.big_cluster_dvfs_opp = opp;
    return trip;
};

}  // namespace

namespace thermal {

template <typename R, typename... Ts>
class FunctionMock {
public:
    FunctionMock& ExpectCall(R ret, Ts... args) {
        has_expectations_ = true;
        expectations_.push_back(Expectation{ret, std::make_tuple(args...)});
        return *this;
    }

    FunctionMock& ExpectNoCall() {
        has_expectations_ = true;
        return *this;
    }

    R Call(Ts... args) {
        R ret = 0;
        CallHelper(&ret, args...);
        return ret;
    }

    bool HasExpectations() const { return has_expectations_; }

    void VerifyAndClear() {
        EXPECT_EQ(expectation_index_, expectations_.size());

        expectations_.reset();
        expectation_index_ = 0;
    }

private:
    struct Expectation {
        R ret_value;
        std::tuple<Ts...> args;
    };

    void CallHelper(R* ret, Ts... args) {
        ASSERT_LT(expectation_index_, expectations_.size());

        Expectation exp = expectations_[expectation_index_++];
        EXPECT_TRUE(exp.args == std::make_tuple(args...));
        *ret = exp.ret_value;
    }

    bool has_expectations_ = false;
    fbl::Vector<Expectation> expectations_;
    size_t expectation_index_ = 0;
};

template <typename... Ts>
class FunctionMock<void, Ts...> {
public:
    FunctionMock& ExpectCall(Ts... args) {
        has_expectations_ = true;
        expectations_.push_back(std::make_tuple(args...));
        return *this;
    }

    FunctionMock& ExpectNoCall() {
        has_expectations_ = true;
        return *this;
    }

    void Call(Ts... args) { CallHelper(args...); }

    bool HasExpectations() const { return has_expectations_; }

    void VerifyAndClear() {
        ASSERT_EQ(expectation_index_, expectations_.size());

        expectations_.reset();
        expectation_index_ = 0;
    }

private:
    void CallHelper(Ts... args) {
        ASSERT_LT(expectation_index_, expectations_.size());
        EXPECT_TRUE(expectations_[expectation_index_++] == std::make_tuple(args...));
    }

    bool has_expectations_ = false;
    fbl::Vector<std::tuple<Ts...>> expectations_;
    size_t expectation_index_ = 0;
};

class MtkThermalTest : public MtkThermal {
public:
    MtkThermalTest(mmio_buffer_t dummy_mmio, const ddk::ClkProtocolClient& clk, uint32_t clk_count,
                   const thermal_device_info_t& thermal_info, zx::port port,
                   TempCalibration0 cal0_fuse, TempCalibration1 cal1_fuse,
                   TempCalibration2 cal2_fuse)
        : MtkThermal(nullptr, ddk::MmioBuffer(dummy_mmio), ddk::MmioBuffer(dummy_mmio),
                     ddk::MmioBuffer(dummy_mmio), ddk::MmioBuffer(dummy_mmio), clk, clk_count,
                     thermal_info, std::move(port), zx::interrupt(), cal0_fuse, cal1_fuse,
                     cal2_fuse),
          mock_thermal_regs_(thermal_reg_array_, sizeof(uint32_t), MT8167_THERMAL_SIZE),
          mock_pll_regs_(pll_reg_array_, sizeof(uint32_t), MT8167_AP_MIXED_SYS_SIZE),
          mock_pmic_wrap_regs_(pmic_wrap_reg_array_, sizeof(uint32_t), MT8167_PMIC_WRAP_SIZE),
          mock_infracfg_regs_(infracfg_reg_array_, sizeof(uint32_t), MT8167_INFRACFG_SIZE) {
        mmio_ = ddk::MmioBuffer(mock_thermal_regs_.GetMmioBuffer());
        pll_mmio_ = ddk::MmioBuffer(mock_pll_regs_.GetMmioBuffer());
        pmic_mmio_ = ddk::MmioBuffer(mock_pmic_wrap_regs_.GetMmioBuffer());
        infracfg_mmio_ = ddk::MmioBuffer(mock_infracfg_regs_.GetMmioBuffer());
    }

    static bool Create(const thermal_device_info_t thermal_info, zx::port port,
                       fbl::unique_ptr<MtkThermalTest>* test) {
        mmio_buffer_t dummy_mmio;
        dummy_mmio.vaddr = &dummy_mmio;
        dummy_mmio.size = sizeof(dummy_mmio);

        TempCalibration0 cal0_fuse;
        cal0_fuse.set_reg_value(kCal0Fuse);

        TempCalibration1 cal1_fuse;
        cal1_fuse.set_reg_value(kCal1Fuse);

        TempCalibration2 cal2_fuse;
        cal2_fuse.set_reg_value(kCal2Fuse);

        fbl::AllocChecker ac;
        test->reset(new (&ac) MtkThermalTest(dummy_mmio, ddk::ClkProtocolClient(), 0, thermal_info,
                                             std::move(port), cal0_fuse, cal1_fuse, cal2_fuse));
        return ac.check();
    }

    ddk_mock::MockMmioRegRegion& thermal_regs() { return mock_thermal_regs_; }
    ddk_mock::MockMmioRegRegion& pll_regs() { return mock_pll_regs_; }
    ddk_mock::MockMmioRegRegion& pmic_wrap_regs() { return mock_pmic_wrap_regs_; }
    ddk_mock::MockMmioRegRegion& infracfg_regs() { return mock_infracfg_regs_; }

    FunctionMock<void, uint16_t, uint32_t>& mock_PmicWrite() { return mock_pmic_write_; }
    FunctionMock<uint32_t>& mock_GetTemperature() { return mock_get_temperature_; }
    FunctionMock<zx_status_t, uint16_t, uint32_t>& mock_SetDvfsOpp() { return mock_set_dvfs_opp_; }
    FunctionMock<zx_status_t, size_t>& mock_SetTripPoint() { return mock_set_trip_point_; }

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

    uint32_t GetTemperature() override {
        if (mock_get_temperature_.HasExpectations()) {
            return mock_get_temperature_.Call();
        } else {
            return MtkThermal::GetTemperature();
        }
    }

    zx_status_t SetDvfsOpp(const dvfs_info_t* opp) override {
        if (mock_set_dvfs_opp_.HasExpectations()) {
            return mock_set_dvfs_opp_.Call(opp->op_idx, opp->power_domain);
        } else {
            return MtkThermal::SetDvfsOpp(opp);
        }
    }

    zx_status_t SetTripPoint(size_t trip_pt) override {
        if (mock_set_trip_point_.HasExpectations()) {
            return mock_set_trip_point_.Call(trip_pt);
        } else {
            return MtkThermal::SetTripPoint(trip_pt);
        }
    }

    // Trigger count interrupts and wait for them to be handled.
    void TriggerInterrupts(uint32_t count) {
        {
            fbl::AutoLock lock(&interrupt_count_lock_);
            interrupt_count_ += count;
        }

        sync_completion_signal(&signal_);

        for (;;) {
            fbl::AutoLock lock(&interrupt_count_lock_);
            if (interrupt_count_ == 0) {
                break;
            }
        }
    }

    void StopThread() override {
        thread_stop_ = true;
        sync_completion_signal(&signal_);
        JoinThread();
    }

private:
    // These were taken from a real device.
    static constexpr uint32_t kCal0Fuse = 0x29389d67;
    static constexpr uint32_t kCal1Fuse = 0x805f84a9;
    static constexpr uint32_t kCal2Fuse = 0x4eaad600;

    zx_status_t WaitForInterrupt() override {
        while (!thread_stop_) {
            {
                fbl::AutoLock lock(&interrupt_count_lock_);
                if (interrupt_count_ > 0) {
                    interrupt_count_--;
                    return ZX_OK;
                }
            }

            // Wait for the main thread to send an update with TriggerInterrupts or StopThread.
            sync_completion_wait(&signal_, ZX_TIME_INFINITE);
            sync_completion_reset(&signal_);
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

    FunctionMock<void, uint16_t, uint32_t> mock_pmic_write_;
    FunctionMock<uint32_t> mock_get_temperature_;
    FunctionMock<zx_status_t, uint16_t, uint32_t> mock_set_dvfs_opp_;
    FunctionMock<zx_status_t, size_t> mock_set_trip_point_;

    volatile bool thread_stop_ = false;
    sync_completion_t signal_;
    uint32_t interrupt_count_ TA_GUARDED(interrupt_count_lock_) = 0;
    fbl::Mutex interrupt_count_lock_;
};

TEST(ThermalTest, TripPoints) {
    thermal_device_info_t thermal_info;
    thermal_info.num_trip_points = 3;
    thermal_info.critical_temp = CToKTenths(50);
    thermal_info.trip_point_info[0] = TripPoint(20, 2);
    thermal_info.trip_point_info[1] = TripPoint(30, 1);
    thermal_info.trip_point_info[2] = TripPoint(40, 0);

    fbl::unique_ptr<MtkThermalTest> test;
    ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

    test->mock_SetDvfsOpp().ExpectNoCall();

    uint32_t up_int = TempMonIntStatus::Get().FromValue(0).set_hot_0(1).reg_value();
    uint32_t down_int = TempMonIntStatus::Get().FromValue(0).set_cold_0(1).reg_value();

    test->mock_GetTemperature().ExpectCall(CToKTenths(20));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);

    test->mock_GetTemperature().ExpectCall(CToKTenths(35));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 1);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(45));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(25));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 1);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(15));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

    EXPECT_OK(test->StartThread());

    test->TriggerInterrupts(4);
    test->StopThread();
    test->VerifyAll();
}

TEST(ThermalTest, CriticalTemperature) {
    thermal_device_info_t thermal_info;
    thermal_info.num_trip_points = 3;
    thermal_info.critical_temp = CToKTenths(50);
    thermal_info.trip_point_info[0] = TripPoint(20, 2);
    thermal_info.trip_point_info[1] = TripPoint(30, 1);
    thermal_info.trip_point_info[2] = TripPoint(40, 0);

    fbl::unique_ptr<MtkThermalTest> test;
    ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

    test->mock_SetDvfsOpp().ExpectNoCall();

    uint32_t critical_int = TempMonIntStatus::Get().FromValue(0).set_stage_3(1).reg_value();

    test->mock_GetTemperature().ExpectCall(CToKTenths(20));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);

    test->mock_GetTemperature().ExpectCall(CToKTenths(55));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);
    test->mock_SetDvfsOpp().ExpectCall(ZX_OK, 0, BIG_CLUSTER_POWER_DOMAIN);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(critical_int);

    EXPECT_OK(test->StartThread());

    test->TriggerInterrupts(1);
    test->StopThread();
    test->VerifyAll();
}

TEST(ThermalTest, InitialTripPoint) {
    thermal_device_info_t thermal_info;
    thermal_info.num_trip_points = 3;
    thermal_info.critical_temp = CToKTenths(50);
    thermal_info.trip_point_info[0] = TripPoint(20, 2);
    thermal_info.trip_point_info[1] = TripPoint(30, 1);
    thermal_info.trip_point_info[2] = TripPoint(40, 0);

    fbl::unique_ptr<MtkThermalTest> test;
    ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

    test->mock_GetTemperature().ExpectCall(CToKTenths(45));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);

    EXPECT_OK(test->StartThread());

    test->StopThread();
    test->VerifyAll();
}

TEST(ThermalTest, TripPointJumpMultiple) {
    thermal_device_info_t thermal_info;
    thermal_info.num_trip_points = 5;
    thermal_info.critical_temp = CToKTenths(100);
    thermal_info.trip_point_info[0] = TripPoint(20, 4);
    thermal_info.trip_point_info[1] = TripPoint(30, 3);
    thermal_info.trip_point_info[2] = TripPoint(40, 2);
    thermal_info.trip_point_info[3] = TripPoint(50, 1);
    thermal_info.trip_point_info[4] = TripPoint(60, 0);

    fbl::unique_ptr<MtkThermalTest> test;
    ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

    uint32_t up_int = TempMonIntStatus::Get().FromValue(0).set_hot_0(1).reg_value();
    uint32_t down_int = TempMonIntStatus::Get().FromValue(0).set_cold_0(1).reg_value();

    test->mock_GetTemperature().ExpectCall(CToKTenths(20));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);

    test->mock_GetTemperature().ExpectCall(CToKTenths(45));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(65));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 4);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(15));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(55));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 3);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(25));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 1);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(65));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 4);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(up_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(35));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 2);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

    test->mock_GetTemperature().ExpectCall(CToKTenths(15));
    test->mock_SetTripPoint().ExpectCall(ZX_OK, 0);
    GetMockReg<TempMonIntStatus>(test->thermal_regs()).ExpectRead(down_int);

    EXPECT_OK(test->StartThread());

    test->TriggerInterrupts(8);
    test->StopThread();
    test->VerifyAll();
}

TEST(ThermalTest, SetTripPoint) {
    thermal_device_info_t thermal_info;
    thermal_info.num_trip_points = 3;
    thermal_info.trip_point_info[0] = TripPoint(20, 2);
    thermal_info.trip_point_info[1] = TripPoint(30, 1);
    thermal_info.trip_point_info[2] = TripPoint(40, 0);

    zx::port port;
    ASSERT_OK(zx::port::create(0, &port));

    fbl::unique_ptr<MtkThermalTest> test;
    ASSERT_TRUE(MtkThermalTest::Create(thermal_info, std::move(port), &test));

    size_t actual = 0;
    ASSERT_OK(test->DdkIoctl(IOCTL_THERMAL_GET_STATE_CHANGE_PORT, nullptr, 0,
                             port.reset_and_get_address(), sizeof(zx_handle_t), &actual));
    ASSERT_EQ(sizeof(zx_handle_t), actual);

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
    thermal_device_info_t thermal_info;
    thermal_info.opps[BIG_CLUSTER_POWER_DOMAIN].count = 3;
    thermal_info.opps[BIG_CLUSTER_POWER_DOMAIN].opp[0] = {598'000'000, 1'150'000};
    thermal_info.opps[BIG_CLUSTER_POWER_DOMAIN].opp[1] = {747'500'000, 1'150'000};
    thermal_info.opps[BIG_CLUSTER_POWER_DOMAIN].opp[2] = {1'040'000'000, 1'200'000};

    fbl::unique_ptr<MtkThermalTest> test;
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

    dvfs_info_t opp{1, BIG_CLUSTER_POWER_DOMAIN};
    EXPECT_EQ(ZX_OK,
              test->DdkIoctl(IOCTL_THERMAL_SET_DVFS_OPP, &opp, sizeof(opp), nullptr, 0, nullptr));

    uint32_t domain = BIG_CLUSTER_POWER_DOMAIN;
    uint32_t opp_out;
    size_t actual = 0;
    EXPECT_OK(test->DdkIoctl(IOCTL_THERMAL_GET_DVFS_OPP, &domain, sizeof(domain), &opp_out,
                             sizeof(opp_out), &actual));
    EXPECT_EQ(sizeof(opp_out), actual);
    EXPECT_EQ(opp.op_idx, opp_out);

    ASSERT_NO_FATAL_FAILURES(test->VerifyAll());

    test->mock_PmicWrite().ExpectCall(voltage_to_step(1'200'000), 0x110);

    GetMockReg<ArmPllCon1>(test->pll_regs())
        .ExpectRead(frequency_to_reg_value(747'500'000))
        .ExpectWrite(frequency_to_reg_value(1'040'000'000));

    opp = {2, BIG_CLUSTER_POWER_DOMAIN};
    EXPECT_OK(test->DdkIoctl(IOCTL_THERMAL_SET_DVFS_OPP, &opp, sizeof(opp), nullptr, 0, nullptr));
    EXPECT_OK(test->DdkIoctl(IOCTL_THERMAL_GET_DVFS_OPP, &domain, sizeof(domain), &opp_out,
                             sizeof(opp_out), &actual));
    EXPECT_EQ(sizeof(opp_out), actual);
    EXPECT_EQ(opp.op_idx, opp_out);

    ASSERT_NO_FATAL_FAILURES(test->VerifyAll());

    test->mock_PmicWrite().ExpectCall(voltage_to_step(1150000), 0x110);

    GetMockReg<ArmPllCon1>(test->pll_regs())
        .ExpectRead(frequency_to_reg_value(1'040'000'000))
        .ExpectWrite(frequency_to_reg_value(598'000'000));

    opp = {0, BIG_CLUSTER_POWER_DOMAIN};
    EXPECT_OK(test->DdkIoctl(IOCTL_THERMAL_SET_DVFS_OPP, &opp, sizeof(opp), nullptr, 0, nullptr));
    EXPECT_OK(test->DdkIoctl(IOCTL_THERMAL_GET_DVFS_OPP, &domain, sizeof(domain), &opp_out,
                             sizeof(opp_out), &actual));
    EXPECT_EQ(sizeof(opp_out), actual);
    EXPECT_EQ(opp.op_idx, opp_out);

    test->VerifyAll();
}

TEST(ThermalTest, DvfsOppVoltageRange) {
    thermal_device_info_t thermal_info;
    thermal_info.opps[BIG_CLUSTER_POWER_DOMAIN].count = 1;
    thermal_info.opps[BIG_CLUSTER_POWER_DOMAIN].opp[0] = {1'000'000'000, 100'000};

    fbl::unique_ptr<MtkThermalTest> test;
    ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));

    dvfs_info_t opp{1, BIG_CLUSTER_POWER_DOMAIN};
    EXPECT_NE(ZX_OK,
              test->DdkIoctl(IOCTL_THERMAL_SET_DVFS_OPP, &opp, sizeof(opp), nullptr, 0, nullptr));

    thermal_info.opps[BIG_CLUSTER_POWER_DOMAIN].opp[0] = {1'000'000'000, 1'500'000};
    ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));
    EXPECT_NE(ZX_OK,
              test->DdkIoctl(IOCTL_THERMAL_SET_DVFS_OPP, &opp, sizeof(opp), nullptr, 0, nullptr));

    thermal_info.opps[BIG_CLUSTER_POWER_DOMAIN].opp[0] = {1'000'000'000, 1'151'000};
    ASSERT_TRUE(MtkThermalTest::Create(thermal_info, zx::port(), &test));
    EXPECT_NE(ZX_OK,
              test->DdkIoctl(IOCTL_THERMAL_SET_DVFS_OPP, &opp, sizeof(opp), nullptr, 0, nullptr));
}

TEST(ThermalTest, PmicWrite) {
    fbl::unique_ptr<MtkThermalTest> test;
    ASSERT_TRUE(MtkThermalTest::Create({}, zx::port(), &test));

    GetMockReg<PmicReadData>(test->pmic_wrap_regs())
        .ExpectRead(0x00060000)
        .ExpectRead(0x00060000)
        .ExpectRead(0x00060000)
        .ExpectRead(0x00000000);

    GetMockReg<PmicCmd>(test->pmic_wrap_regs()).ExpectWrite(0xce8761df);

    test->PmicWrite(0x61df, 0x4e87);
    ASSERT_NO_FATAL_FAILURES(test->VerifyAll());

    GetMockReg<PmicReadData>(test->pmic_wrap_regs())
        .ExpectRead(0x00060000)
        .ExpectRead(0x00000000);

    GetMockReg<PmicCmd>(test->pmic_wrap_regs()).ExpectWrite(0xf374504f);

    test->PmicWrite(0x504f, 0x7374);
    test->VerifyAll();
}

}  // namespace thermal

int main(int argc, char** argv) {
    return RUN_ALL_TESTS(argc, argv);
}
