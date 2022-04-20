// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwc3.h"

#include <fake-mmio-reg/fake-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "dwc3-regs.h"
#include "src/devices/bus/testing/fake-pdev/fake-pdev.h"
#include "src/devices/testing/mock-ddk/mock-device.h"

namespace dwc3 {

class TestFixture : public zxtest::Test {
 public:
  TestFixture();
  void SetUp() override;

  fake_pdev::FakePDev::MmioInfo mmio_info() { return {.offset = reinterpret_cast<size_t>(this)}; }
  fdf::MmioBuffer mmio() { return fdf::MmioBuffer(reg_region_.GetMmioBuffer()); }

 protected:
  static constexpr size_t kRegSize = sizeof(uint32_t);
  static constexpr size_t kMmioRegionSize = 64 << 10;
  static constexpr size_t kRegCount = kMmioRegionSize / kRegSize;

  // Section 1.3.22 of the DWC3 Programmer's guide
  //
  // DWC_USB31_CACHE_TOTAL_XFER_RESOURCES : 32
  // DWC_USB31_NUM_IN_EPS                 : 16
  // DWC_USB31_NUM_EPS                    : 32
  // DWC_USB31_VENDOR_CTL_INTERFACE       : 0
  // DWC_USB31_HSPHY_DWIDTH               : 2
  // DWC_USB31_HSPHY_INTERFACE            : 1
  // DWC_USB31_SSPHY_INTERFACE            : 2
  //
  uint64_t Read_GHWPARAMS3() { return 0x10420086; }

  // Section 1.3.45 of the DWC3 Programmer's guide
  uint64_t Read_USB31_VER_NUMBER() { return 0x31363061; }  // 1.60a

  // Section 1.4.2 of the DWC3 Programmer's guide
  uint64_t Read_DCTL() { return dctl_val_; }
  void Write_DCTL(uint64_t val) {
    constexpr uint32_t kUnwriteableMask =
        (1 << 29) | (1 << 17) | (1 << 16) | (1 << 15) | (1 << 14) | (1 << 13) | (1 << 0);
    ZX_DEBUG_ASSERT(val <= std::numeric_limits<uint32_t>::max());
    dctl_val_ = static_cast<uint32_t>(val & ~kUnwriteableMask);

    // Immediately clear the soft reset bit if we are not testing the soft reset
    // timeout behavior.
    if (!stuck_reset_test_) {
      dctl_val_ = DCTL::Get().FromValue(dctl_val_).set_CSFTRST(0).reg_value();
    }
  }

  uint32_t dctl_val_ = DCTL::Get().FromValue(0).set_LPM_NYET_thres(0xF).reg_value();
  bool stuck_reset_test_{false};

  std::shared_ptr<MockDevice> mock_parent_{MockDevice::FakeRootParent()};
  fake_pdev::FakePDev fake_pdev_{};
  std::array<ddk_fake::FakeMmioReg, kRegCount> registers_;
  ddk_fake::FakeMmioRegRegion reg_region_{registers_.data(), kRegSize, registers_.size()};
};

TestFixture::TestFixture() {
  auto& hwparams3 = registers_[GHWPARAMS3::Get().addr() / sizeof(uint32_t)];
  auto& ver_reg = registers_[USB31_VER_NUMBER::Get().addr() / sizeof(uint32_t)];
  auto& dctl_reg = registers_[DCTL::Get().addr() / sizeof(uint32_t)];

  hwparams3.SetReadCallback([this]() -> uint64_t { return Read_GHWPARAMS3(); });
  ver_reg.SetReadCallback([this]() -> uint64_t { return Read_USB31_VER_NUMBER(); });
  dctl_reg.SetReadCallback([this]() -> uint64_t { return Read_DCTL(); });
  dctl_reg.SetWriteCallback([this](uint64_t val) { return Write_DCTL(val); });

  fake_pdev_.set_mmio(0, mmio_info());
  fake_pdev_.UseFakeBti(true);
  fake_pdev_.CreateVirtualInterrupt(0);

  // TODO(johngro): Lifecycle?  How is this managed by the testing framework?
  mock_parent_->AddProtocol(ZX_PROTOCOL_PDEV, fake_pdev_.proto()->ops, fake_pdev_.proto()->ctx);
}

void TestFixture::SetUp() { stuck_reset_test_ = false; }

TEST_F(TestFixture, DdkLifecycle) {
  ASSERT_OK(Dwc3::Create(nullptr, mock_parent_.get()));

  // make sure the child device is there
  ASSERT_EQ(1, mock_parent_->child_count());
  auto* child = mock_parent_->GetLatestChild();

  child->InitOp();
  EXPECT_TRUE(child->InitReplyCalled());
  EXPECT_OK(child->InitReplyCallStatus());

  child->UnbindOp();
  EXPECT_TRUE(child->UnbindReplyCalled());

  child->ReleaseOp();
}

TEST_F(TestFixture, DdkHwResetTimeout) {
  stuck_reset_test_ = true;
  ASSERT_OK(Dwc3::Create(nullptr, mock_parent_.get()));

  // make sure the child device is there
  ASSERT_EQ(1, mock_parent_->child_count());
  auto* child = mock_parent_->GetLatestChild();

  child->InitOp();
  EXPECT_TRUE(child->InitReplyCalled());
  EXPECT_STATUS(ZX_ERR_TIMED_OUT, child->InitReplyCallStatus());

  child->UnbindOp();
  EXPECT_TRUE(child->UnbindReplyCalled());

  child->ReleaseOp();
}

}  // namespace dwc3

// Redefine PDevMakeMmioBufferWeak per the recommendation in pdev.h.
zx_status_t ddk::PDevMakeMmioBufferWeak(const pdev_mmio_t& pdev_mmio,
                                        std::optional<MmioBuffer>* mmio, uint32_t cache_policy) {
  auto* test_harness = reinterpret_cast<dwc3::TestFixture*>(pdev_mmio.offset);
  mmio->emplace(test_harness->mmio());
  return ZX_OK;
}
