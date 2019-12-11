// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-canvas.h"

#include <lib/fake-bti/bti.h>
#include <lib/fake_ddk/fake_ddk.h>

#include <vector>

#include <mock-mmio-reg/mock-mmio-reg.h>
#include <zxtest/zxtest.h>

#include "dmc-regs.h"

namespace {
constexpr uint32_t kMmioRegSize = sizeof(uint32_t);
constexpr uint32_t kMmioRegCount = (aml_canvas::kDmcCavMaxRegAddr + kMmioRegSize) / kMmioRegSize;
constexpr uint32_t kVmoTestSize = PAGE_SIZE;

constexpr canvas_info_t test_canvas_info = []() {
  canvas_info_t ci = {};
  ci.height = 240;
  ci.stride_bytes = 16;
  return ci;
}();

constexpr canvas_info_t invalid_canvas_info = []() {
  canvas_info_t ci = {};
  ci.height = 240;
  ci.stride_bytes = 15;
  return ci;
}();
}  // namespace

namespace aml_canvas {

template <typename T>
ddk_mock::MockMmioReg& GetMockReg(ddk_mock::MockMmioRegRegion& registers) {
  return registers[T::Get().addr()];
}

class AmlCanvasTest : public zxtest::Test {
 public:
  AmlCanvasTest()
      : mock_regs_(ddk_mock::MockMmioRegRegion(mock_reg_array_, kMmioRegSize, kMmioRegCount)) {
    ddk::MmioBuffer mmio(mock_regs_.GetMmioBuffer());

    zx::bti bti;
    EXPECT_OK(fake_bti_create(bti.reset_and_get_address()));
    fake_bti_ = zx::bti(bti.get());

    fbl::AllocChecker ac;
    canvas_ = fbl::make_unique_checked<AmlCanvas>(&ac, fake_ddk::kFakeParent, std::move(mmio),
                                                  std::move(bti));
    EXPECT_TRUE(ac.check());
  }

  ~AmlCanvasTest() {
    if (fake_bti_.is_valid()) {
      fake_bti_destroy(fake_bti_.get());
    }
  }

  void TestLifecycle() {
    fake_ddk::Bind ddk;
    EXPECT_OK(canvas_->DdkAdd("aml-canvas"));
    canvas_->DdkAsyncRemove();
    EXPECT_TRUE(ddk.Ok());
    canvas_->DdkRelease();
    __UNUSED auto ptr = canvas_.release();
  }

  zx_status_t CreateNewCanvas() {
    zx::vmo vmo;
    EXPECT_OK(zx::vmo::create(kVmoTestSize, 0, &vmo));

    uint8_t index;
    zx_status_t status = canvas_->AmlogicCanvasConfig(std::move(vmo), 0, &test_canvas_info, &index);
    if (status != ZX_OK) {
      return status;
    }
    canvas_indices_.push_back(index);
    return status;
  }

  zx_status_t CreateNewCanvasInvalid() {
    zx::vmo vmo;
    EXPECT_OK(zx::vmo::create(kVmoTestSize, 0, &vmo));

    uint8_t index;
    return canvas_->AmlogicCanvasConfig(std::move(vmo), 0, &invalid_canvas_info, &index);
  }

  zx_status_t FreeCanvas(uint8_t index) {
    auto it = std::find(canvas_indices_.begin(), canvas_indices_.end(), index);
    if (it != canvas_indices_.end()) {
      canvas_indices_.erase(it);
    }
    return canvas_->AmlogicCanvasFree(index);
  }

  zx_status_t FreeAllCanvases() {
    zx_status_t status = ZX_OK;
    while (!canvas_indices_.empty()) {
      uint8_t index = canvas_indices_.back();
      canvas_indices_.pop_back();
      status = canvas_->AmlogicCanvasFree(index);
      if (status != ZX_OK) {
        return status;
      }
    }
    return status;
  }

  void SetRegisterExpectations() {
    GetMockReg<CanvasLutDataLow>(mock_regs_).ExpectWrite(CanvasLutDataLowValue());
    GetMockReg<CanvasLutDataHigh>(mock_regs_).ExpectWrite(CanvasLutDataHighValue());
    GetMockReg<CanvasLutAddr>(mock_regs_).ExpectWrite(CanvasLutAddrValue(NextCanvasIndex()));
  }

  void SetRegisterExpectations(uint8_t index) {
    GetMockReg<CanvasLutDataLow>(mock_regs_).ExpectWrite(CanvasLutDataLowValue());
    GetMockReg<CanvasLutDataHigh>(mock_regs_).ExpectWrite(CanvasLutDataHighValue());
    GetMockReg<CanvasLutAddr>(mock_regs_).ExpectWrite(CanvasLutAddrValue(index));
  }

  void VerifyAll() {
    GetMockReg<CanvasLutDataLow>(mock_regs_).VerifyAndClear();
    GetMockReg<CanvasLutDataHigh>(mock_regs_).VerifyAndClear();
    GetMockReg<CanvasLutAddr>(mock_regs_).VerifyAndClear();
  }

 private:
  uint8_t NextCanvasIndex() { return static_cast<uint8_t>(canvas_indices_.size()); }

  uint32_t CanvasLutDataLowValue() {
    auto data_low = CanvasLutDataLow::Get().FromValue(0);
    data_low.SetDmcCavWidth(test_canvas_info.stride_bytes >> 3);
    data_low.set_dmc_cav_addr(FAKE_BTI_PHYS_ADDR >> 3);
    return data_low.reg_value();
  }

  uint32_t CanvasLutDataHighValue() {
    auto data_high = CanvasLutDataHigh::Get().FromValue(0);
    data_high.SetDmcCavWidth(test_canvas_info.stride_bytes >> 3);
    data_high.set_dmc_cav_height(test_canvas_info.height);
    data_high.set_dmc_cav_blkmode(test_canvas_info.blkmode);
    data_high.set_dmc_cav_xwrap(test_canvas_info.wrap & CanvasLutDataHigh::kDmcCavXwrap ? 1 : 0);
    data_high.set_dmc_cav_ywrap(test_canvas_info.wrap & CanvasLutDataHigh::kDmcCavYwrap ? 1 : 0);
    data_high.set_dmc_cav_endianness(test_canvas_info.endianness);
    return data_high.reg_value();
  }

  uint32_t CanvasLutAddrValue(uint8_t index) {
    auto lut_addr = CanvasLutAddr::Get().FromValue(0);
    lut_addr.set_dmc_cav_addr_index(index);
    lut_addr.set_dmc_cav_addr_wr(1);
    return lut_addr.reg_value();
  }

  std::vector<uint8_t> canvas_indices_;
  ddk_mock::MockMmioReg mock_reg_array_[kMmioRegCount];
  ddk_mock::MockMmioRegRegion mock_regs_;
  zx::bti fake_bti_;
  std::unique_ptr<AmlCanvas> canvas_;
};

TEST_F(AmlCanvasTest, DdkLifecyle) { TestLifecycle(); }

TEST_F(AmlCanvasTest, CanvasConfigFreeSingle) {
  SetRegisterExpectations();
  EXPECT_OK(CreateNewCanvas());
  ASSERT_NO_FATAL_FAILURES(VerifyAll());

  EXPECT_OK(FreeAllCanvases());
}

TEST_F(AmlCanvasTest, CanvasConfigFreeMultipleSequential) {
  // Create 5 canvases in sequence and verify that their indices are 0 through 4.
  for (int i = 0; i < 5; i++) {
    SetRegisterExpectations();
    EXPECT_OK(CreateNewCanvas());
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  // Free all 5 canvases created above.
  EXPECT_OK(FreeAllCanvases());
}

TEST_F(AmlCanvasTest, CanvasConfigFreeMultipleInterleaved) {
  // Create 5 canvases in sequence.
  for (int i = 0; i < 5; i++) {
    SetRegisterExpectations();
    EXPECT_OK(CreateNewCanvas());
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  // Free canvas index 1, so the next one created has index 1.
  EXPECT_OK(FreeCanvas(1));

  SetRegisterExpectations(1);
  EXPECT_OK(CreateNewCanvas());
  ASSERT_NO_FATAL_FAILURES(VerifyAll());

  // Free canvas index 3, so the next one created has index 3.
  EXPECT_OK(FreeCanvas(3));

  SetRegisterExpectations(3);
  EXPECT_OK(CreateNewCanvas());
  ASSERT_NO_FATAL_FAILURES(VerifyAll());

  EXPECT_OK(FreeAllCanvases());
}

TEST_F(AmlCanvasTest, CanvasFreeInvalidIndex) {
  // Free a canvas without having created any.
  EXPECT_EQ(FreeCanvas(0), ZX_ERR_INVALID_ARGS);
}

TEST_F(AmlCanvasTest, CanvasConfigMaxLimit) {
  // Create canvases until the look-up table is full.
  for (size_t i = 0; i < kNumCanvasEntries; i++) {
    SetRegisterExpectations();
    EXPECT_OK(CreateNewCanvas());
    ASSERT_NO_FATAL_FAILURES(VerifyAll());
  }

  // Try to create another canvas, and verify that it fails.
  EXPECT_EQ(CreateNewCanvas(), ZX_ERR_NOT_FOUND);

  EXPECT_OK(FreeAllCanvases());
}

TEST_F(AmlCanvasTest, CanvasConfigUnaligned) {
  // Try to create a canvas with unaligned canvas_info_t width, and verify that it fails.
  EXPECT_EQ(CreateNewCanvasInvalid(), ZX_ERR_INVALID_ARGS);
}

}  //  namespace aml_canvas
