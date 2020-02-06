// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../gamma-rgb-registers.h"

#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "../../mali-009/pingpong_regs.h"

namespace camera {
namespace {

constexpr uint32_t kDefaultGain = 256;
constexpr uint32_t kTestGain = 2560;
constexpr uint32_t kDefaultOffset = 0;
constexpr uint32_t kTestOffset = 10;

constexpr size_t kRegCount = 5;

class GammaRgbRegistersTest : public zxtest::Test {
 public:
  void SetUp() override;

  void ResetDefaults();

  void SetExpectations();

 protected:
  GammaRgbRegisterDefs reg_defs_;

  bool enable_val_;
  uint32_t gain_r_val_;
  uint32_t gain_g_val_;
  uint32_t gain_b_val_;
  uint32_t offset_r_val_;
  uint32_t offset_g_val_;
  uint32_t offset_b_val_;

  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_registers_;
  std::unique_ptr<GammaRgbRegisters> registers_;
};

void GammaRgbRegistersTest::ResetDefaults() {
  enable_val_ = true;
  gain_r_val_ = kDefaultGain;
  gain_g_val_ = kDefaultGain;
  gain_b_val_ = kDefaultGain;
  offset_r_val_ = kDefaultOffset;
  offset_g_val_ = kDefaultOffset;
  offset_b_val_ = kDefaultOffset;
}

void GammaRgbRegistersTest::SetExpectations() {
  (*mock_registers_)[0x00].ExpectWrite(true).ExpectWrite(enable_val_);
  (*mock_registers_)[0x04]
      .ExpectWrite((kDefaultGain << 16) + kDefaultGain)
      .ExpectWrite((gain_g_val_ << 16) + gain_r_val_);
  (*mock_registers_)[0x08].ExpectWrite(kDefaultGain).ExpectWrite(gain_b_val_);
  (*mock_registers_)[0x0c]
      .ExpectWrite((kDefaultOffset << 16) + kDefaultOffset)
      .ExpectWrite((offset_g_val_ << 16) + offset_r_val_);
  (*mock_registers_)[0x10].ExpectWrite(kDefaultOffset).ExpectWrite(offset_b_val_);
}

void GammaRgbRegistersTest::SetUp() { ResetDefaults(); }

TEST_F(GammaRgbRegistersTest, TestEnable) {
  fbl::Array<ddk_mock::MockMmioReg> reg_array =
      fbl::Array(new ddk_mock::MockMmioReg[kRegCount], kRegCount);
  mock_registers_ =
      std::make_unique<ddk_mock::MockMmioRegRegion>(reg_array.data(), sizeof(uint32_t), kRegCount);
  ddk::MmioView local_mmio(mock_registers_->GetMmioBuffer().View(0));
  registers_ = std::make_unique<GammaRgbRegisters>(local_mmio);
  enable_val_ = false;
  SetExpectations();
  registers_->Init();
  registers_->SetEnable(enable_val_);
  registers_->WriteRegisters();
  mock_registers_->VerifyAll();
}

TEST_F(GammaRgbRegistersTest, TestSetGainR) {
  fbl::Array<ddk_mock::MockMmioReg> reg_array =
      fbl::Array(new ddk_mock::MockMmioReg[kRegCount], kRegCount);
  mock_registers_ =
      std::make_unique<ddk_mock::MockMmioRegRegion>(reg_array.data(), sizeof(uint32_t), kRegCount);
  ddk::MmioView local_mmio(mock_registers_->GetMmioBuffer().View(0));
  registers_ = std::make_unique<GammaRgbRegisters>(local_mmio);
  gain_r_val_ = kTestGain;
  SetExpectations();
  registers_->Init();
  registers_->SetGainR(gain_r_val_);
  registers_->WriteRegisters();
  mock_registers_->VerifyAll();
}

TEST_F(GammaRgbRegistersTest, TestSetGainG) {
  fbl::Array<ddk_mock::MockMmioReg> reg_array =
      fbl::Array(new ddk_mock::MockMmioReg[kRegCount], kRegCount);
  mock_registers_ =
      std::make_unique<ddk_mock::MockMmioRegRegion>(reg_array.data(), sizeof(uint32_t), kRegCount);
  ddk::MmioView local_mmio(mock_registers_->GetMmioBuffer().View(0));
  registers_ = std::make_unique<GammaRgbRegisters>(local_mmio);
  gain_g_val_ = kTestGain;
  SetExpectations();
  registers_->Init();
  registers_->SetGainG(gain_g_val_);
  registers_->WriteRegisters();
  mock_registers_->VerifyAll();
}

TEST_F(GammaRgbRegistersTest, TestSetGainB) {
  fbl::Array<ddk_mock::MockMmioReg> reg_array =
      fbl::Array(new ddk_mock::MockMmioReg[kRegCount], kRegCount);
  mock_registers_ =
      std::make_unique<ddk_mock::MockMmioRegRegion>(reg_array.data(), sizeof(uint32_t), kRegCount);
  ddk::MmioView local_mmio(mock_registers_->GetMmioBuffer().View(0));
  registers_ = std::make_unique<GammaRgbRegisters>(local_mmio);
  gain_b_val_ = kTestGain;
  SetExpectations();
  registers_->Init();
  registers_->SetGainB(gain_b_val_);
  registers_->WriteRegisters();
  mock_registers_->VerifyAll();
}

TEST_F(GammaRgbRegistersTest, TestSetOffsetR) {
  fbl::Array<ddk_mock::MockMmioReg> reg_array =
      fbl::Array(new ddk_mock::MockMmioReg[kRegCount], kRegCount);
  mock_registers_ =
      std::make_unique<ddk_mock::MockMmioRegRegion>(reg_array.data(), sizeof(uint32_t), kRegCount);
  ddk::MmioView local_mmio(mock_registers_->GetMmioBuffer().View(0));
  registers_ = std::make_unique<GammaRgbRegisters>(local_mmio);
  offset_r_val_ = kTestOffset;
  SetExpectations();
  registers_->Init();
  registers_->SetOffsetR(offset_r_val_);
  registers_->WriteRegisters();
  mock_registers_->VerifyAll();
}

TEST_F(GammaRgbRegistersTest, TestSetOffsetG) {
  fbl::Array<ddk_mock::MockMmioReg> reg_array =
      fbl::Array(new ddk_mock::MockMmioReg[kRegCount], kRegCount);
  mock_registers_ =
      std::make_unique<ddk_mock::MockMmioRegRegion>(reg_array.data(), sizeof(uint32_t), kRegCount);
  ddk::MmioView local_mmio(mock_registers_->GetMmioBuffer().View(0));
  registers_ = std::make_unique<GammaRgbRegisters>(local_mmio);
  offset_g_val_ = kTestOffset;
  SetExpectations();
  registers_->Init();
  registers_->SetOffsetG(offset_g_val_);
  registers_->WriteRegisters();
  mock_registers_->VerifyAll();
}

TEST_F(GammaRgbRegistersTest, TestSetOffsetB) {
  fbl::Array<ddk_mock::MockMmioReg> reg_array =
      fbl::Array(new ddk_mock::MockMmioReg[kRegCount], kRegCount);
  mock_registers_ =
      std::make_unique<ddk_mock::MockMmioRegRegion>(reg_array.data(), sizeof(uint32_t), kRegCount);
  ddk::MmioView local_mmio(mock_registers_->GetMmioBuffer().View(0));
  registers_ = std::make_unique<GammaRgbRegisters>(local_mmio);
  offset_b_val_ = kTestOffset;
  SetExpectations();
  registers_->Init();
  registers_->SetOffsetB(offset_b_val_);
  registers_->WriteRegisters();
  mock_registers_->VerifyAll();
}

}  // namespace
}  // namespace camera
