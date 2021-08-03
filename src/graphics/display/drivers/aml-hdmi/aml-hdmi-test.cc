// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "aml-hdmi.h"

#include <lib/fake_ddk/fidl-helper.h>

#include <queue>

#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>

namespace aml_hdmi {

namespace {

constexpr size_t kRegSize = 0x00100000 / sizeof(uint32_t);  // in 32 bits chunks.

}  // namespace

using HdmiClient = fidl::WireSyncClient<fuchsia_hardware_hdmi::Hdmi>;
using fuchsia_hardware_hdmi::wire::ColorDepth;
using fuchsia_hardware_hdmi::wire::ColorFormat;

enum class HdmiDwFn {
  kConfigHdmitx,
  kSetupInterrupts,
  kReset,
  kSetupScdc,
  kResetFc,
  kSetFcScramblerCtrl,
};

class AmlHdmiTest;

class FakeHdmiDw : public hdmi_dw::HdmiDw {
 public:
  explicit FakeHdmiDw(HdmiIpBase* base, AmlHdmiTest* test) : HdmiDw(base), test_(test) {}

  void ConfigHdmitx(const fuchsia_hardware_hdmi::wire::DisplayMode& mode,
                    const hdmi_dw::hdmi_param_tx& p) override;
  void SetupInterrupts() override;
  void Reset() override;
  void SetupScdc(bool is4k) override;
  void ResetFc() override;
  void SetFcScramblerCtrl(bool is4k) override;

 private:
  AmlHdmiTest* test_;
};

class FakeAmlHdmiDevice : public AmlHdmiDevice {
 public:
  static std::unique_ptr<FakeAmlHdmiDevice> Create(AmlHdmiTest* test, ddk::MmioBuffer mmio) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakeAmlHdmiDevice>(&ac, test, std::move(mmio));
    if (!ac.check()) {
      zxlogf(ERROR, "device object alloc failed");
      return nullptr;
    }
    return device;
  }

  explicit FakeAmlHdmiDevice(AmlHdmiTest* test, ddk::MmioBuffer mmio)
      : AmlHdmiDevice(nullptr, std::move(mmio), std::make_unique<FakeHdmiDw>(this, test)) {}

  static zx_status_t MessageOp(void* ctx, fidl_incoming_msg_t* msg, fidl_txn_t* txn) {
    return static_cast<AmlHdmiDevice*>(ctx)->ddk_device_proto_.message(ctx, msg, txn);
  }
  zx_status_t InitTest() { return messenger_.SetMessageOp(this, FakeAmlHdmiDevice::MessageOp); }
  zx::channel& GetMessengerChannel() { return messenger_.local(); }

 private:
  fake_ddk::FidlMessenger messenger_;
};

class AmlHdmiTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "regs_ alloc failed");
      return;
    }
    mock_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(&ac, regs_.get(),
                                                                       sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: mock_mmio_ alloc failed", __func__);
      return;
    }

    ddk::MmioBuffer mmio(mock_mmio_->GetMmioBuffer());

    dut_ = FakeAmlHdmiDevice::Create(this, std::move(mmio));
    ASSERT_NOT_NULL(dut_);
    ASSERT_OK(dut_->InitTest());

    hdmi_client_ = std::make_unique<HdmiClient>(
        fidl::ClientEnd<fuchsia_hardware_hdmi::Hdmi>(std::move(dut_->GetMessengerChannel())));
  }

  void TearDown() override {
    ASSERT_TRUE(expected_dw_calls_.size() == 0);
    mock_mmio_->VerifyAll();
  }

  void HdmiDwCall(HdmiDwFn func) {
    ASSERT_FALSE(expected_dw_calls_.empty());
    ASSERT_EQ(expected_dw_calls_.front(), func);
    expected_dw_calls_.pop();
  }
  void ExpectHdmiDwConfigHdmitx() { expected_dw_calls_.push(HdmiDwFn::kConfigHdmitx); }
  void ExpectHdmiDwSetupInterrupts() { expected_dw_calls_.push(HdmiDwFn::kSetupInterrupts); }
  void ExpectHdmiDwReset() { expected_dw_calls_.push(HdmiDwFn::kReset); }
  void ExpectHdmiDwSetupScdc() { expected_dw_calls_.push(HdmiDwFn::kSetupScdc); }
  void ExpectHdmiDwResetFc() { expected_dw_calls_.push(HdmiDwFn::kResetFc); }
  void ExpectHdmiDwSetFcScramblerCtrl() { expected_dw_calls_.push(HdmiDwFn::kSetFcScramblerCtrl); }

 protected:
  std::unique_ptr<FakeAmlHdmiDevice> dut_;
  std::unique_ptr<HdmiClient> hdmi_client_;

  fbl::Array<ddk_mock::MockMmioReg> regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio_;

  std::queue<HdmiDwFn> expected_dw_calls_;
};

void FakeHdmiDw::ConfigHdmitx(const fuchsia_hardware_hdmi::wire::DisplayMode& mode,
                              const hdmi_dw::hdmi_param_tx& p) {
  test_->HdmiDwCall(HdmiDwFn::kConfigHdmitx);
}

void FakeHdmiDw::SetupInterrupts() { test_->HdmiDwCall(HdmiDwFn::kSetupInterrupts); }

void FakeHdmiDw::Reset() { test_->HdmiDwCall(HdmiDwFn::kReset); }

void FakeHdmiDw::SetupScdc(bool is4k) { test_->HdmiDwCall(HdmiDwFn::kSetupScdc); }

void FakeHdmiDw::ResetFc() { test_->HdmiDwCall(HdmiDwFn::kResetFc); }

void FakeHdmiDw::SetFcScramblerCtrl(bool is4k) { test_->HdmiDwCall(HdmiDwFn::kSetFcScramblerCtrl); }

TEST_F(AmlHdmiTest, ReadTest) {
  // Amlogic Register
  (*mock_mmio_)[0x12 * 4 + 0x8000].ExpectRead(0x1234);
  auto pval_aml = hdmi_client_->ReadReg(0x12);
  ASSERT_OK(pval_aml.status());
  EXPECT_EQ(pval_aml->val, 0x1234);

  // Designware Register
  (*mock_mmio_)[0x3].ExpectRead(0x21);
  auto pval_dwc = hdmi_client_->ReadReg((0x10UL << 24) + 0x3);
  ASSERT_OK(pval_dwc.status());
  EXPECT_EQ(pval_dwc->val, 0x21);
}

TEST_F(AmlHdmiTest, WriteTest) {
  // Amlogic Register
  (*mock_mmio_)[0x5 * 4 + 0x8000].ExpectWrite(0x4321);
  auto pres_aml = hdmi_client_->WriteReg(0x5, 0x4321);
  ASSERT_OK(pres_aml.status());

  // Designware Register
  (*mock_mmio_)[0x420].ExpectWrite(0x15);
  auto pres_dwc = hdmi_client_->WriteReg((0x10UL << 24) + 0x420, 0x2415);
  ASSERT_OK(pres_dwc.status());
}

TEST_F(AmlHdmiTest, ResetTest) {
  (*mock_mmio_)[0x0 * 4 + 0x8000].ExpectWrite(0);     // HDMITX_TOP_SW_RESET
  (*mock_mmio_)[0x1 * 4 + 0x8000].ExpectWrite(0xff);  // HDMITX_TOP_CLK_CNTL
  auto pres = hdmi_client_->Reset(1);
  ASSERT_OK(pres.status());
}

TEST_F(AmlHdmiTest, ModeSetTest) {
  fidl::Arena allocator;
  fuchsia_hardware_hdmi::wire::StandardDisplayMode standard_display_mode{
      .pixel_clock_10khz = 0,
      .h_addressable = 0,
      .h_front_porch = 0,
      .h_sync_pulse = 0,
      .h_blanking = 0,
      .v_addressable = 0,
      .v_front_porch = 0,
      .v_sync_pulse = 0,
      .v_blanking = 0,
      .flags = 0,
  };
  fuchsia_hardware_hdmi::wire::ColorParam color{
      .input_color_format = ColorFormat::kCfRgb,
      .output_color_format = ColorFormat::kCfRgb,
      .color_depth = ColorDepth::kCd24B,
  };
  fuchsia_hardware_hdmi::wire::DisplayMode mode(allocator);
  mode.set_mode(allocator, standard_display_mode);
  mode.set_color(allocator, color);

  (*mock_mmio_)[0x6 * 4 + 0x8000].ExpectWrite(1 << 12);  // HDMITX_TOP_BIST_CNTL
  ExpectHdmiDwConfigHdmitx();
  (*mock_mmio_)[0x5 * 4 + 0x8000].ExpectWrite(0x1f);  // HDMITX_TOP_INTR_STAT_CLR
  ExpectHdmiDwSetupInterrupts();
  (*mock_mmio_)[0x3 * 4 + 0x8000].ExpectWrite(0x9f);  // HDMITX_TOP_INTR_MASKN
  ExpectHdmiDwReset();

  (*mock_mmio_)[0xA * 4 + 0x8000].ExpectWrite(0x001f001f);  // HDMITX_TOP_TMDS_CLK_PTTN_01
  (*mock_mmio_)[0xB * 4 + 0x8000].ExpectWrite(0x001f001f);  // HDMITX_TOP_TMDS_CLK_PTTN_23
  ExpectHdmiDwSetFcScramblerCtrl();

  (*mock_mmio_)[0xC * 4 + 0x8000].ExpectWrite(0x1);  // HDMITX_TOP_TMDS_CLK_PTTN_CNTL
  (*mock_mmio_)[0xC * 4 + 0x8000].ExpectWrite(0x2);  // HDMITX_TOP_TMDS_CLK_PTTN_CNTL

  ExpectHdmiDwSetupScdc();
  ExpectHdmiDwResetFc();
  auto pres = hdmi_client_->ModeSet(1, mode);
  ASSERT_OK(pres.status());
}

}  // namespace aml_hdmi
