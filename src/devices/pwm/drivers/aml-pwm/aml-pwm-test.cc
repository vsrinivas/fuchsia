// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-pwm.h"

#include <lib/fake_ddk/fake_ddk.h>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>

namespace {

constexpr size_t kRegSize = 0x00001000 / sizeof(uint32_t);  // in 32 bits chunks.

}  // namespace

namespace pwm {

class FakeAmlPwmDevice : public AmlPwmDevice {
 public:
  static std::unique_ptr<FakeAmlPwmDevice> Create(ddk::MmioBuffer mmio0, ddk::MmioBuffer mmio1,
                                                  ddk::MmioBuffer mmio2, ddk::MmioBuffer mmio3,
                                                  ddk::MmioBuffer mmio4) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakeAmlPwmDevice>(&ac);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: device object alloc failed", __func__);
      return nullptr;
    }
    device->Init(std::move(mmio0), std::move(mmio1), std::move(mmio2), std::move(mmio3),
                 std::move(mmio4));

    return device;
  }

  explicit FakeAmlPwmDevice() : AmlPwmDevice() {}
};

class AmlPwmDeviceTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    regs0_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: regs0_ alloc failed", __func__);
      return;
    }
    mock_mmio0_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(&ac, regs0_.get(),
                                                                        sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio0_ alloc failed", __func__);
      return;
    }
    regs1_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: regs1_ alloc failed", __func__);
      return;
    }
    mock_mmio1_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(&ac, regs1_.get(),
                                                                        sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio1_ alloc failed", __func__);
      return;
    }
    regs2_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: regs2_ alloc failed", __func__);
      return;
    }
    mock_mmio2_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(&ac, regs2_.get(),
                                                                        sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio2_ alloc failed", __func__);
      return;
    }
    regs3_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: regs3_ alloc failed", __func__);
      return;
    }
    mock_mmio3_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(&ac, regs3_.get(),
                                                                        sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio3_ alloc failed", __func__);
      return;
    }
    regs4_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: regs4_ alloc failed", __func__);
      return;
    }
    mock_mmio4_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(&ac, regs4_.get(),
                                                                        sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio4_ alloc failed", __func__);
      return;
    }

    (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFDFFFFFA);  // SetMode
    (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFEFFFFF5);  // SetMode
    (*mock_mmio1_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFDFFFFFA);  // SetMode
    (*mock_mmio1_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFEFFFFF5);  // SetMode
    (*mock_mmio2_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFDFFFFFA);  // SetMode
    (*mock_mmio2_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFEFFFFF5);  // SetMode
    (*mock_mmio3_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFDFFFFFA);  // SetMode
    (*mock_mmio3_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFEFFFFF5);  // SetMode
    (*mock_mmio4_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFDFFFFFA);  // SetMode
    (*mock_mmio4_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFEFFFFF5);  // SetMode
    ddk::MmioBuffer mmio0(mock_mmio0_->GetMmioBuffer());
    ddk::MmioBuffer mmio1(mock_mmio1_->GetMmioBuffer());
    ddk::MmioBuffer mmio2(mock_mmio2_->GetMmioBuffer());
    ddk::MmioBuffer mmio3(mock_mmio3_->GetMmioBuffer());
    ddk::MmioBuffer mmio4(mock_mmio4_->GetMmioBuffer());
    pwm_ = FakeAmlPwmDevice::Create(std::move(mmio0), std::move(mmio1), std::move(mmio2),
                                    std::move(mmio3), std::move(mmio4));
    ASSERT_NOT_NULL(pwm_);
  }

  void TearDown() override {
    mock_mmio0_->VerifyAll();
    mock_mmio1_->VerifyAll();
    mock_mmio2_->VerifyAll();
    mock_mmio3_->VerifyAll();
    mock_mmio4_->VerifyAll();
  }

 protected:
  std::unique_ptr<FakeAmlPwmDevice> pwm_;

  // Mmio Regs and Regions
  fbl::Array<ddk_mock::MockMmioReg> regs0_;
  fbl::Array<ddk_mock::MockMmioReg> regs1_;
  fbl::Array<ddk_mock::MockMmioReg> regs2_;
  fbl::Array<ddk_mock::MockMmioReg> regs3_;
  fbl::Array<ddk_mock::MockMmioReg> regs4_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio0_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio1_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio2_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio3_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio4_;
};

TEST_F(AmlPwmDeviceTest, GetConfigTest) {
  mode_config mode_cfg{
      .mode = 100,
      .regular = {},
  };
  pwm_config cfg{
      .polarity = false,
      .period_ns = 1250,
      .duty_cycle = 100.0,
      .mode_config_buffer = &mode_cfg,
      .mode_config_size = sizeof(mode_cfg),
  };
  EXPECT_OK(pwm_->PwmImplGetConfig(0, &cfg));

  cfg.mode_config_buffer = nullptr;
  EXPECT_NOT_OK(pwm_->PwmImplGetConfig(0, &cfg));
}

TEST_F(AmlPwmDeviceTest, SetConfigTest) {
  EXPECT_NOT_OK(pwm_->PwmImplSetConfig(0, nullptr));  // Fail

  mode_config fail{
      .mode = 100,
      .regular = {},
  };
  pwm_config fail_cfg{
      .polarity = false,
      .period_ns = 1250,
      .duty_cycle = 100.0,
      .mode_config_buffer = &fail,
      .mode_config_size = sizeof(fail),
  };
  EXPECT_NOT_OK(pwm_->PwmImplSetConfig(0, &fail_cfg));  // Fail

  for (uint32_t i = 0; i < UNKNOWN; i++) {
    mode_config fail{
        .mode = i,
        .regular = {},
    };
    pwm_config fail_cfg{
        .polarity = false,
        .period_ns = 1250,
        .duty_cycle = 100.0,
        .mode_config_buffer = &fail,
        .mode_config_size = sizeof(fail),
    };
    EXPECT_NOT_OK(pwm_->PwmImplSetConfig(10, &fail_cfg));  // Fail
  }

  // OFF
  mode_config off{
      .mode = OFF,
      .regular = {},
  };
  pwm_config off_cfg{
      .polarity = false,
      .period_ns = 1250,
      .duty_cycle = 100.0,
      .mode_config_buffer = &off,
      .mode_config_size = sizeof(off),
  };
  EXPECT_OK(pwm_->PwmImplSetConfig(0, &off_cfg));

  (*mock_mmio0_)[2 * 4].ExpectRead(0x01000000).ExpectWrite(0x01000001);  // SetMode
  (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFBFFFFFF);  // Invert
  (*mock_mmio0_)[2 * 4].ExpectRead(0x00000000).ExpectWrite(0x10000000);  // EnableConst
  (*mock_mmio0_)[0 * 4].ExpectRead(0xA39D9259).ExpectWrite(0x001E0000);  // SetDutyCycle
  mode_config on{
      .mode = ON,
      .regular = {},
  };
  pwm_config on_cfg{
      .polarity = false,
      .period_ns = 1250,
      .duty_cycle = 100.0,
      .mode_config_buffer = &on,
      .mode_config_size = sizeof(on),
  };
  EXPECT_OK(pwm_->PwmImplSetConfig(0, &on_cfg));  // turn on

  (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFDFFFFFA);  // SetMode
  EXPECT_OK(pwm_->PwmImplSetConfig(0, &off_cfg));
  EXPECT_OK(pwm_->PwmImplSetConfig(0, &off_cfg));  // same configs

  // ON
  (*mock_mmio0_)[2 * 4].ExpectRead(0x01000000).ExpectWrite(0x00000002);  // SetMode
  (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xF7FFFFFF);  // Invert
  (*mock_mmio0_)[2 * 4].ExpectRead(0x00000000).ExpectWrite(0x20000000);  // EnableConst
  (*mock_mmio0_)[1 * 4].ExpectRead(0xA39D9259).ExpectWrite(0x001E0000);  // SetDutyCycle
  EXPECT_OK(pwm_->PwmImplSetConfig(1, &on_cfg));

  (*mock_mmio0_)[2 * 4].ExpectRead(0x00000000).ExpectWrite(0x08000000);  // Invert
  (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xDFFFFFFF);  // EnableConst
  (*mock_mmio0_)[1 * 4].ExpectRead(0xA39D9259).ExpectWrite(0x00060010);  // SetDutyCycle
  on_cfg.polarity = true;
  on_cfg.period_ns = 1000;
  on_cfg.duty_cycle = 30.0;
  EXPECT_OK(pwm_->PwmImplSetConfig(1, &on_cfg));  // Change Duty Cycle

  (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFEFFFFF5);  // SetMode
  EXPECT_OK(pwm_->PwmImplSetConfig(1, &off_cfg));                        // Change Mode

  // DELTA_SIGMA
  (*mock_mmio1_)[2 * 4].ExpectRead(0x02000000).ExpectWrite(0x00000004);  // SetMode
  (*mock_mmio1_)[3 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFF0064);  // SetDSSetting
  (*mock_mmio1_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFBFFFFFF);  // Invert
  (*mock_mmio1_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xEFFFFFFF);  // EnableConst
  (*mock_mmio1_)[0 * 4].ExpectRead(0xA39D9259).ExpectWrite(0x00060010);  // SetDutyCycle
  mode_config ds{
      .mode = DELTA_SIGMA,
      .delta_sigma =
          {
              .delta = 100,
          },
  };
  pwm_config ds_cfg{
      .polarity = false,
      .period_ns = 1000,
      .duty_cycle = 30.0,
      .mode_config_buffer = &ds,
      .mode_config_size = sizeof(ds),
  };
  EXPECT_OK(pwm_->PwmImplSetConfig(2, &ds_cfg));

  // TWO_TIMER
  (*mock_mmio3_)[2 * 4].ExpectRead(0x00000000).ExpectWrite(0x01000002);  // SetMode
  (*mock_mmio3_)[6 * 4].ExpectRead(0xA39D9259).ExpectWrite(0x00130003);  // SetDutyCycle2
  (*mock_mmio3_)[4 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFFFF0302);  // SetTimers
  (*mock_mmio3_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xF7FFFFFF);  // Invert
  (*mock_mmio3_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xDFFFFFFF);  // EnableConst
  (*mock_mmio3_)[1 * 4].ExpectRead(0xA39D9259).ExpectWrite(0x00060010);  // SetDutyCycle
  mode_config timer2{
      .mode = TWO_TIMER,
      .two_timer =
          {
              .period_ns2 = 1000,
              .duty_cycle2 = 80.0,
              .timer1 = 3,
              .timer2 = 2,
          },
  };
  pwm_config timer2_cfg{
      .polarity = false,
      .period_ns = 1000,
      .duty_cycle = 30.0,
      .mode_config_buffer = &timer2,
      .mode_config_size = sizeof(timer2),
  };
  EXPECT_OK(pwm_->PwmImplSetConfig(7, &timer2_cfg));
}

TEST_F(AmlPwmDeviceTest, SetConfigFailTest) {
  (*mock_mmio0_)[2 * 4].ExpectRead(0x01000000).ExpectWrite(0x01000001);  // SetMode
  (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFBFFFFFF);  // Invert
  (*mock_mmio0_)[2 * 4].ExpectRead(0x00000000).ExpectWrite(0x10000000);  // EnableConst
  (*mock_mmio0_)[0 * 4].ExpectRead(0xA39D9259).ExpectWrite(0x001E0000);  // SetDutyCycle
  mode_config on{
      .mode = ON,
      .regular = {},
  };
  pwm_config on_cfg{
      .polarity = false,
      .period_ns = 1250,
      .duty_cycle = 100.0,
      .mode_config_buffer = &on,
      .mode_config_size = sizeof(on),
  };
  EXPECT_OK(pwm_->PwmImplSetConfig(0, &on_cfg));  // Success

  (*mock_mmio0_)[2 * 4].ExpectRead(0x00000000).ExpectWrite(0x04000000);  // Invert
  (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xEFFFFFFF);  // EnableConst
                                                                         // Fail
  (*mock_mmio0_)[2 * 4].ExpectRead(0xFFFFFFFF).ExpectWrite(0xFBFFFFFF);  // Invert
  (*mock_mmio0_)[2 * 4].ExpectRead(0x00000000).ExpectWrite(0x10000000);  // EnableConst
  (*mock_mmio0_)[0 * 4].ExpectRead(0xA39D9259).ExpectWrite(0x001E0000);  // SetDutyCycle
  on_cfg.polarity = true;
  on_cfg.duty_cycle = 120.0;
  EXPECT_NOT_OK(pwm_->PwmImplSetConfig(0, &on_cfg));  // Fail
}

TEST_F(AmlPwmDeviceTest, EnableTest) {
  EXPECT_NOT_OK(pwm_->PwmImplEnable(10));  // Fail

  (*mock_mmio1_)[2 * 4].ExpectRead(0x00000000).ExpectWrite(0x00008000);
  EXPECT_OK(pwm_->PwmImplEnable(2));
  EXPECT_OK(pwm_->PwmImplEnable(2));  // Enable twice

  (*mock_mmio2_)[2 * 4].ExpectRead(0x00008000).ExpectWrite(0x00808000);
  EXPECT_OK(pwm_->PwmImplEnable(5));  // Enable other PWMs
}

TEST_F(AmlPwmDeviceTest, DisableTest) {
  EXPECT_NOT_OK(pwm_->PwmImplDisable(10));  // Fail

  EXPECT_OK(pwm_->PwmImplDisable(0));  // Disable first

  (*mock_mmio0_)[2 * 4].ExpectRead(0x00000000).ExpectWrite(0x00008000);
  EXPECT_OK(pwm_->PwmImplEnable(0));

  (*mock_mmio0_)[2 * 4].ExpectRead(0x00008000).ExpectWrite(0x00000000);
  EXPECT_OK(pwm_->PwmImplDisable(0));
  EXPECT_OK(pwm_->PwmImplDisable(0));  // Disable twice

  (*mock_mmio2_)[2 * 4].ExpectRead(0x00008000).ExpectWrite(0x00808000);
  EXPECT_OK(pwm_->PwmImplEnable(5));  // Enable other PWMs

  (*mock_mmio2_)[2 * 4].ExpectRead(0x00808000).ExpectWrite(0x00008000);
  EXPECT_OK(pwm_->PwmImplDisable(5));  // Disable other PWMs
}

}  // namespace pwm
