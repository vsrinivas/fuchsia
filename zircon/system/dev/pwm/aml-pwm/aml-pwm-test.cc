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
  static std::unique_ptr<FakeAmlPwmDevice> Create(ddk::MmioBuffer mmio_ab, ddk::MmioBuffer mmio_cd,
                                                  ddk::MmioBuffer mmio_ef,
                                                  ddk::MmioBuffer mmio_ao_ab,
                                                  ddk::MmioBuffer mmio_ao_cd) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakeAmlPwmDevice>(
        &ac, std::move(mmio_ab), std::move(mmio_cd), std::move(mmio_ef), std::move(mmio_ao_ab),
        std::move(mmio_ao_cd));
    if (!ac.check()) {
      zxlogf(ERROR, "%s: device object alloc failed\n", __func__);
      return nullptr;
    }

    return device;
  }

  explicit FakeAmlPwmDevice(ddk::MmioBuffer mmio_ab, ddk::MmioBuffer mmio_cd,
                            ddk::MmioBuffer mmio_ef, ddk::MmioBuffer mmio_ao_ab,
                            ddk::MmioBuffer mmio_ao_cd)
      : AmlPwmDevice(std::move(mmio_ab), std::move(mmio_cd), std::move(mmio_ef),
                     std::move(mmio_ao_ab), std::move(mmio_ao_cd)) {}
};

class AmlPwmDeviceTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    ab_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: ab_regs_ alloc failed\n", __func__);
      return;
    }
    mock_mmio_ab_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, ab_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio_ab_ alloc failed\n", __func__);
      return;
    }
    cd_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: cd_regs_ alloc failed\n", __func__);
      return;
    }
    mock_mmio_cd_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, cd_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio_cd_ alloc failed\n", __func__);
      return;
    }
    ef_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: ef_regs_ alloc failed\n", __func__);
      return;
    }
    mock_mmio_ef_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, ef_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio_ef_ alloc failed\n", __func__);
      return;
    }
    ao_ab_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: ao_ab_regs_ alloc failed\n", __func__);
      return;
    }
    mock_mmio_ao_ab_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, ao_ab_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio_ao_ab_ alloc failed\n", __func__);
      return;
    }
    ao_cd_regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: ao_cd_regs_ alloc failed\n", __func__);
      return;
    }
    mock_mmio_ao_cd_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(
        &ac, ao_cd_regs_.get(), sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio_ao_cd_ alloc failed\n", __func__);
      return;
    }

    ddk::MmioBuffer mmio_ab(mock_mmio_ab_->GetMmioBuffer());
    ddk::MmioBuffer mmio_cd(mock_mmio_cd_->GetMmioBuffer());
    ddk::MmioBuffer mmio_ef(mock_mmio_ef_->GetMmioBuffer());
    ddk::MmioBuffer mmio_ao_ab(mock_mmio_ao_ab_->GetMmioBuffer());
    ddk::MmioBuffer mmio_ao_cd(mock_mmio_ao_cd_->GetMmioBuffer());
    pwm_ = FakeAmlPwmDevice::Create(std::move(mmio_ab), std::move(mmio_cd), std::move(mmio_ef),
                                    std::move(mmio_ao_ab), std::move(mmio_ao_cd));
    ASSERT_NOT_NULL(pwm_);
  }

  void TearDown() override {
    mock_mmio_ab_->VerifyAll();
    mock_mmio_cd_->VerifyAll();
    mock_mmio_ef_->VerifyAll();
    mock_mmio_ao_ab_->VerifyAll();
    mock_mmio_ao_cd_->VerifyAll();
  }

 protected:
  std::unique_ptr<FakeAmlPwmDevice> pwm_;

  // Mmio Regs and Regions
  fbl::Array<ddk_mock::MockMmioReg> ab_regs_;
  fbl::Array<ddk_mock::MockMmioReg> cd_regs_;
  fbl::Array<ddk_mock::MockMmioReg> ef_regs_;
  fbl::Array<ddk_mock::MockMmioReg> ao_ab_regs_;
  fbl::Array<ddk_mock::MockMmioReg> ao_cd_regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio_ab_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio_cd_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio_ef_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio_ao_ab_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio_ao_cd_;
};

TEST_F(AmlPwmDeviceTest, SetConfigTest) {
  EXPECT_EQ(pwm_->PwmImplSetConfig(0, nullptr), ZX_ERR_NOT_SUPPORTED);
}

TEST_F(AmlPwmDeviceTest, EnableTest) { EXPECT_EQ(pwm_->PwmImplDisable(0), ZX_ERR_NOT_SUPPORTED); }

TEST_F(AmlPwmDeviceTest, DisableTest) { EXPECT_EQ(pwm_->PwmImplDisable(0), ZX_ERR_NOT_SUPPORTED); }

}  // namespace pwm
