// Copyright 2020 The Fuchsia Authors. All rights reserved.  Use of
// this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/adc/llcpp/fidl.h>
#include <fuchsia/hardware/temperature/llcpp/fidl.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <cmath>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <soc/aml-common/aml-g12-saradc.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <zxtest/zxtest.h>

#include "../thermistor.h"

namespace {

bool FloatNear(float a, float b) { return std::abs(a - b) < 0.1f; }

}  // namespace

namespace thermal {

using TemperatureClient = ::llcpp::fuchsia::hardware::temperature::Device::SyncClient;
using AdcClient = ::llcpp::fuchsia::hardware::adc::Device::SyncClient;

NtcInfo ntc_info[] = {
    {.part = "ncpXXwf104",
     .profile =
         {
             {.temperature_c = -40, .resistance_ohm = 4397119},  // 0
             {.temperature_c = -35, .resistance_ohm = 3088599},
             {.temperature_c = -30, .resistance_ohm = 2197225},
             {.temperature_c = -25, .resistance_ohm = 1581881},
             {.temperature_c = -20, .resistance_ohm = 1151037},
             {.temperature_c = -15, .resistance_ohm = 846579},
             {.temperature_c = -10, .resistance_ohm = 628988},
             {.temperature_c = -5, .resistance_ohm = 471632},
             {.temperature_c = 0, .resistance_ohm = 357012},
             {.temperature_c = 5, .resistance_ohm = 272500},
             {.temperature_c = 10, .resistance_ohm = 209710},  // 10
             {.temperature_c = 15, .resistance_ohm = 162651},
             {.temperature_c = 20, .resistance_ohm = 127080},
             {.temperature_c = 25, .resistance_ohm = 100000},
             {.temperature_c = 30, .resistance_ohm = 79222},
             {.temperature_c = 35, .resistance_ohm = 63167},
             {.temperature_c = 40, .resistance_ohm = 50677},
             {.temperature_c = 45, .resistance_ohm = 40904},
             {.temperature_c = 50, .resistance_ohm = 33195},
             {.temperature_c = 55, .resistance_ohm = 27091},
             {.temperature_c = 60, .resistance_ohm = 22224},  // 20
             {.temperature_c = 65, .resistance_ohm = 18323},
             {.temperature_c = 70, .resistance_ohm = 15184},
             {.temperature_c = 75, .resistance_ohm = 12635},
             {.temperature_c = 80, .resistance_ohm = 10566},
             {.temperature_c = 85, .resistance_ohm = 8873},
             {.temperature_c = 90, .resistance_ohm = 7481},
             {.temperature_c = 95, .resistance_ohm = 6337},
             {.temperature_c = 100, .resistance_ohm = 5384},
             {.temperature_c = 105, .resistance_ohm = 4594},
             {.temperature_c = 110, .resistance_ohm = 3934},  // 30
             {.temperature_c = 115, .resistance_ohm = 3380},
             {.temperature_c = 120, .resistance_ohm = 2916},
             {.temperature_c = 125, .resistance_ohm = 2522},  // 33
         }},
};

class TestSarAdc : public AmlSaradcDevice {
 public:
  static constexpr uint32_t kMaxChannels = 4;
  TestSarAdc(ddk::MmioBuffer adc_mmio, ddk::MmioBuffer ao_mmio, zx::interrupt irq)
      : AmlSaradcDevice(std::move(adc_mmio), std::move(ao_mmio), std::move(irq)) {}
  void HwInit() override {}
  void Shutdown() override {}

  zx_status_t GetSample(uint32_t channel, uint32_t* outval) override {
    if (channel >= kMaxChannels) {
      return ZX_ERR_INVALID_ARGS;
    }
    *outval = values_[channel];
    return ZX_OK;
  }
  void SetReadValue(uint32_t ch, uint32_t value) { values_[ch] = value; }

 private:
  uint32_t values_[kMaxChannels];
};

class ThermistorDeviceTest : public zxtest::Test {
 public:
  ThermistorDeviceTest() {}

  uint32_t CalcSampleValue(NtcInfo info, uint32_t idx, uint32_t pullup) {
    uint32_t ntc_resistance = info.profile[idx].resistance_ohm;
    float ratio = static_cast<float>(ntc_resistance) / static_cast<float>(ntc_resistance + pullup);
    float sample = round(ratio * ((1 << adc_->Resolution()) - 1));
    return static_cast<uint32_t>(sample);
  }

  void SetUp() override {
    constexpr size_t kRegSize = S905D2_SARADC_LENGTH / sizeof(uint32_t);  // in 32 bits chunks.
    fbl::Array<ddk_mock::MockMmioReg> regs0 =
        fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);
    fbl::Array<ddk_mock::MockMmioReg> regs1 =
        fbl::Array(new ddk_mock::MockMmioReg[kRegSize], kRegSize);

    ddk_mock::MockMmioRegRegion mock0(regs0.data(), sizeof(uint32_t), kRegSize);
    ddk_mock::MockMmioRegRegion mock1(regs1.data(), sizeof(uint32_t), kRegSize);

    zx::interrupt irq;
    adc_ = fbl::MakeRefCounted<TestSarAdc>(mock0.GetMmioBuffer(), mock1.GetMmioBuffer(),
                                           std::move(irq));

    thermistor_ = std::make_unique<ThermistorChannel>(fake_ddk::kFakeParent, adc_, 0, ntc_info[0],
                                                      kPullupValue);

    const auto message_op = [](void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) -> zx_status_t {
      return static_cast<ThermistorChannel*>(ctx)->DdkMessage(msg, txn);
    };
    ASSERT_OK(messenger_.SetMessageOp(thermistor_.get(), message_op));
  }

 protected:
  static constexpr uint32_t kPullupValue = 47000;

  std::unique_ptr<ThermistorChannel> thermistor_;
  fbl::RefPtr<TestSarAdc> adc_;
  fake_ddk::FidlMessenger messenger_;
};

TEST_F(ThermistorDeviceTest, GetTemperatureCelsius) {
  TemperatureClient client(std::move(messenger_.local()));

  {
    uint32_t ntc_idx = 10;
    adc_->SetReadValue(0, CalcSampleValue(ntc_info[0], ntc_idx, kPullupValue));
    auto result = client.GetTemperatureCelsius();
    EXPECT_OK(result->status);
    EXPECT_TRUE(FloatNear(result->temp, ntc_info[0].profile[ntc_idx].temperature_c));
  }

  {  // set read value to 0, which should be out of range of the ntc table
    adc_->SetReadValue(0, 0);
    auto result = client.GetTemperatureCelsius();
    EXPECT_NOT_OK(result->status);
  }

  {  // set read value to max, which should be out of range of ntc table
    adc_->SetReadValue(0, (1 << adc_->Resolution()) - 1);
    auto result = client.GetTemperatureCelsius();
    EXPECT_NOT_OK(result->status);
  }
}

}  //  namespace thermal
