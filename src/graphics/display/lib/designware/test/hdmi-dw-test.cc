// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/hdmi-dw/hdmi-dw.h>

#include <fbl/array.h>
#include <mock-mmio-reg/mock-mmio-reg.h>

namespace hdmi_dw {

namespace {

constexpr size_t kRegSize = 0x00010000 / sizeof(uint8_t);  // in 32 bits chunks.

}  // namespace

using fuchsia_hardware_hdmi::wire::ColorDepth;
using fuchsia_hardware_hdmi::wire::ColorFormat;
using fuchsia_hardware_hdmi::wire::ColorParam;
using fuchsia_hardware_hdmi::wire::StandardDisplayMode;

class FakeHdmiIpBase : public HdmiIpBase {
 public:
  explicit FakeHdmiIpBase(ddk::MmioBuffer mmio) : HdmiIpBase(), mmio_(std::move(mmio)) {}

  void WriteIpReg(uint32_t addr, uint32_t data) { mmio_.Write8(data, addr); }
  uint32_t ReadIpReg(uint32_t addr) { return mmio_.Read8(addr); }

 private:
  ddk::MmioBuffer mmio_;
};

class FakeHdmiDw : public HdmiDw {
 public:
  static std::unique_ptr<FakeHdmiDw> Create(ddk::MmioBuffer mmio) {
    fbl::AllocChecker ac;
    auto device = fbl::make_unique_checked<FakeHdmiDw>(&ac, std::move(mmio));
    if (!ac.check()) {
      zxlogf(ERROR, "%s: device object alloc failed", __func__);
      return nullptr;
    }

    return device;
  }

  explicit FakeHdmiDw(ddk::MmioBuffer mmio) : HdmiDw(&base_), base_(std::move(mmio)) {}

 private:
  FakeHdmiIpBase base_;
};

class HdmiDwTest : public zxtest::Test {
 public:
  void SetUp() override {
    fbl::AllocChecker ac;

    regs_ = fbl::Array(new (&ac) ddk_mock::MockMmioReg[kRegSize], kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: regs_ alloc failed", __func__);
      return;
    }
    mock_mmio_ = fbl::make_unique_checked<ddk_mock::MockMmioRegRegion>(&ac, regs_.get(),
                                                                       sizeof(uint32_t), kRegSize);
    if (!ac.check()) {
      zxlogf(ERROR, "%s: regs_ alloc failed", __func__);
      return;
    }

    hdmi_dw_ = FakeHdmiDw::Create(ddk::MmioBuffer(mock_mmio_->GetMmioBuffer()));
    ASSERT_NOT_NULL(hdmi_dw_);
  }

  void TearDown() override { mock_mmio_->VerifyAll(); }

  void ExpectScdcWrite(uint8_t addr, uint8_t val) {
    (*mock_mmio_)[0x7E00].ExpectWrite(0x54);  // HDMITX_DWC_I2CM_SLAVE
    (*mock_mmio_)[0x7E01].ExpectWrite(addr);  // HDMITX_DWC_I2CM_ADDR
    (*mock_mmio_)[0x7E02].ExpectWrite(val);   // HDMITX_DWC_I2CM_DATAO
    (*mock_mmio_)[0x7E04].ExpectWrite(0x10);  // HDMITX_DWC_I2CM_OPERATION
  }

  void ExpectScdcRead(uint8_t addr, uint8_t val) {
    (*mock_mmio_)[0x7E00].ExpectWrite(0x54);  // HDMITX_DWC_I2CM_SLAVE
    (*mock_mmio_)[0x7E01].ExpectWrite(addr);  // HDMITX_DWC_I2CM_ADDR
    (*mock_mmio_)[0x7E04].ExpectWrite(0x01);  // HDMITX_DWC_I2CM_OPERATION

    (*mock_mmio_)[0x7E03].ExpectRead(val);  // HDMITX_DWC_I2CM_DATAI
  }

 protected:
  std::unique_ptr<FakeHdmiDw> hdmi_dw_;

  // Mmio Regs and Regions
  fbl::Array<ddk_mock::MockMmioReg> regs_;
  std::unique_ptr<ddk_mock::MockMmioRegRegion> mock_mmio_;
};

TEST_F(HdmiDwTest, InitHwTest) {
  (*mock_mmio_)[0x4006].ExpectWrite(0xff);  // HDMITX_DWC_MC_LOCKONCLOCK
  (*mock_mmio_)[0x4001].ExpectWrite(0x00);  // HDMITX_DWC_MC_CLKDIS

  (*mock_mmio_)[0x7E05].ExpectWrite(0x00);  // HDMITX_DWC_I2CM_INT
  (*mock_mmio_)[0x7E06].ExpectWrite(0x00);  // HDMITX_DWC_I2CM_CTLINT

  (*mock_mmio_)[0x7E07].ExpectWrite(0x00);  // HDMITX_DWC_I2CM_DIV

  (*mock_mmio_)[0x7E0B].ExpectWrite(0x00);  // HDMITX_DWC_I2CM_SS_SCL_HCNT_1
  (*mock_mmio_)[0x7E0C].ExpectWrite(0xcf);  // HDMITX_DWC_I2CM_SS_SCL_HCNT_0
  (*mock_mmio_)[0x7E0D].ExpectWrite(0x00);  // HDMITX_DWC_I2CM_SS_SCL_LCNT_1
  (*mock_mmio_)[0x7E0E].ExpectWrite(0xff);  // HDMITX_DWC_I2CM_SS_SCL_LCNT_0
  (*mock_mmio_)[0x7E0F].ExpectWrite(0x00);  // HDMITX_DWC_I2CM_FS_SCL_HCNT_1
  (*mock_mmio_)[0x7E10].ExpectWrite(0x0f);  // HDMITX_DWC_I2CM_FS_SCL_HCNT_0
  (*mock_mmio_)[0x7E11].ExpectWrite(0x00);  // HDMITX_DWC_I2CM_FS_SCL_LCNT_1
  (*mock_mmio_)[0x7E12].ExpectWrite(0x20);  // HDMITX_DWC_I2CM_FS_SCL_LCNT_0
  (*mock_mmio_)[0x7E13].ExpectWrite(0x08);  // HDMITX_DWC_I2CM_SDA_HOLD

  (*mock_mmio_)[0x7E14].ExpectWrite(0x00);  // HDMITX_DWC_I2CM_SCDC_UPDATE

  hdmi_dw_->InitHw();
}

TEST_F(HdmiDwTest, EdidTransferTest) {
  uint8_t in_data[] = {1, 2};
  uint8_t out_data[16] = {0};
  i2c_impl_op_t op_list[]{
      {
          .address = 0x30,
          .data_buffer = &in_data[0],
          .data_size = 1,
          .is_read = false,
          .stop = false,
      },
      {
          .address = 0x50,
          .data_buffer = &in_data[1],
          .data_size = 1,
          .is_read = false,
          .stop = false,
      },
      {
          .address = 0x50,
          .data_buffer = &out_data[0],
          .data_size = 16,
          .is_read = true,
          .stop = true,
      },
  };

  (*mock_mmio_)[0x7E00].ExpectWrite(0x50);  // HDMITX_DWC_I2CM_SLAVE
  (*mock_mmio_)[0x7E08].ExpectWrite(0x30);  // HDMITX_DWC_I2CM_SEGADDR
  (*mock_mmio_)[0x7E0A].ExpectWrite(1);     // HDMITX_DWC_I2CM_SEGPTR

  (*mock_mmio_)[0x7E01].ExpectWrite(2);       // HDMITX_DWC_I2CM_ADDRESS
  (*mock_mmio_)[0x7E04].ExpectWrite(1 << 2);  // HDMITX_DWC_I2CM_OPERATION

  (*mock_mmio_)[0x0105].ExpectRead(0x00).ExpectRead(0xff);  // HDMITX_DWC_IH_I2CM_STAT0

  (*mock_mmio_)[0x0105].ExpectWrite(0x02);  // HDMITX_DWC_IH_I2CM_STAT0

  (*mock_mmio_)[0x7E20].ExpectRead(8);  // HDMITX_DWC_I2CM_READ_BUFF0
  (*mock_mmio_)[0x7E21].ExpectRead(7);  // HDMITX_DWC_I2CM_READ_BUFF1
  (*mock_mmio_)[0x7E22].ExpectRead(6);  // HDMITX_DWC_I2CM_READ_BUFF2
  (*mock_mmio_)[0x7E23].ExpectRead(5);  // HDMITX_DWC_I2CM_READ_BUFF3
  (*mock_mmio_)[0x7E24].ExpectRead(4);  // HDMITX_DWC_I2CM_READ_BUFF4
  (*mock_mmio_)[0x7E25].ExpectRead(3);  // HDMITX_DWC_I2CM_READ_BUFF5
  (*mock_mmio_)[0x7E26].ExpectRead(2);  // HDMITX_DWC_I2CM_READ_BUFF6
  (*mock_mmio_)[0x7E27].ExpectRead(1);  // HDMITX_DWC_I2CM_READ_BUFF7

  (*mock_mmio_)[0x7E01].ExpectWrite(10);      // HDMITX_DWC_I2CM_ADDRESS
  (*mock_mmio_)[0x7E04].ExpectWrite(1 << 2);  // HDMITX_DWC_I2CM_OPERATION

  (*mock_mmio_)[0x0105].ExpectRead(0xff);  // HDMITX_DWC_IH_I2CM_STAT0

  (*mock_mmio_)[0x0105].ExpectWrite(0x02);  // HDMITX_DWC_IH_I2CM_STAT0

  (*mock_mmio_)[0x7E20].ExpectRead(1);  // HDMITX_DWC_I2CM_READ_BUFF0
  (*mock_mmio_)[0x7E21].ExpectRead(2);  // HDMITX_DWC_I2CM_READ_BUFF1
  (*mock_mmio_)[0x7E22].ExpectRead(3);  // HDMITX_DWC_I2CM_READ_BUFF2
  (*mock_mmio_)[0x7E23].ExpectRead(4);  // HDMITX_DWC_I2CM_READ_BUFF3
  (*mock_mmio_)[0x7E24].ExpectRead(5);  // HDMITX_DWC_I2CM_READ_BUFF4
  (*mock_mmio_)[0x7E25].ExpectRead(6);  // HDMITX_DWC_I2CM_READ_BUFF5
  (*mock_mmio_)[0x7E26].ExpectRead(7);  // HDMITX_DWC_I2CM_READ_BUFF6
  (*mock_mmio_)[0x7E27].ExpectRead(8);  // HDMITX_DWC_I2CM_READ_BUFF7

  hdmi_dw_->EdidTransfer(op_list, sizeof(op_list) / sizeof(op_list[0]));
  uint8_t expected_out[] = {8, 7, 6, 5, 4, 3, 2, 1, 1, 2, 3, 4, 5, 6, 7, 8};
  for (uint32_t i = 0; i < 16; i++) {
    EXPECT_EQ(out_data[i], expected_out[i]);
  }
}

TEST_F(HdmiDwTest, ConfigHdmitxTest) {
  fidl::Arena allocator;
  StandardDisplayMode standard_display_mode{
      .pixel_clock_10khz = 30,
      .h_addressable = 24,
      .h_front_porch = 15,
      .h_sync_pulse = 50,
      .h_blanking = 93,
      .v_addressable = 75,
      .v_front_porch = 104,
      .v_sync_pulse = 49,
      .v_blanking = 83,
      .flags = 0,
  };
  ColorParam color{
      .input_color_format = ColorFormat::kCfRgb,
      .output_color_format = ColorFormat::kCf444,
      .color_depth = ColorDepth::kCd30B,
  };
  DisplayMode mode(allocator);
  mode.set_mode(allocator, standard_display_mode);
  mode.set_color(allocator, color);

  hdmi_param_tx p{
      .vic = 9,
      .aspect_ratio = 0,
      .colorimetry = 1,
      .is4K = false,
  };

  (*mock_mmio_)[0x0200].ExpectWrite(0x03);  // HDMITX_DWC_TX_INVID0

  (*mock_mmio_)[0x0201].ExpectWrite(0x00);  // HDMITX_DWC_TX_INSTUFFING
  (*mock_mmio_)[0x0202].ExpectWrite(0x00);  // HDMITX_DWC_TX_GYDATA0
  (*mock_mmio_)[0x0203].ExpectWrite(0x00);  // HDMITX_DWC_TX_GYDATA1
  (*mock_mmio_)[0x0204].ExpectWrite(0x00);  // HDMITX_DWC_TX_RCRDATA0
  (*mock_mmio_)[0x0205].ExpectWrite(0x00);  // HDMITX_DWC_TX_RCRDATA1
  (*mock_mmio_)[0x0206].ExpectWrite(0x00);  // HDMITX_DWC_TX_BCBDATA0
  (*mock_mmio_)[0x0207].ExpectWrite(0x00);  // HDMITX_DWC_TX_BCBDATA1

  // ConfigCsc
  (*mock_mmio_)[0x4004].ExpectWrite(0x01);  // HDMITX_DWC_MC_FLOWCTRL

  (*mock_mmio_)[0x4100].ExpectWrite(0x00);  // HDMITX_DWC_CSC_CFG

  (*mock_mmio_)[0x4102].ExpectWrite(0x25);  // HDMITX_DWC_CSC_COEF_A1_MSB
  (*mock_mmio_)[0x4103].ExpectWrite(0x91);  // HDMITX_DWC_CSC_COEF_A1_LSB
  (*mock_mmio_)[0x4104].ExpectWrite(0x13);  // HDMITX_DWC_CSC_COEF_A2_MSB
  (*mock_mmio_)[0x4105].ExpectWrite(0x23);  // HDMITX_DWC_CSC_COEF_A2_LSB
  (*mock_mmio_)[0x4106].ExpectWrite(0x07);  // HDMITX_DWC_CSC_COEF_A3_MSB
  (*mock_mmio_)[0x4107].ExpectWrite(0x4c);  // HDMITX_DWC_CSC_COEF_A3_LSB
  (*mock_mmio_)[0x4108].ExpectWrite(0x00);  // HDMITX_DWC_CSC_COEF_A4_MSB
  (*mock_mmio_)[0x4109].ExpectWrite(0x00);  // HDMITX_DWC_CSC_COEF_A4_LSB
  (*mock_mmio_)[0x410A].ExpectWrite(0xe5);  // HDMITX_DWC_CSC_COEF_B1_MSB
  (*mock_mmio_)[0x410B].ExpectWrite(0x34);  // HDMITX_DWC_CSC_COEF_B1_LSB
  (*mock_mmio_)[0x410C].ExpectWrite(0x20);  // HDMITX_DWC_CSC_COEF_B2_MSB
  (*mock_mmio_)[0x410D].ExpectWrite(0x00);  // HDMITX_DWC_CSC_COEF_B2_LSB
  (*mock_mmio_)[0x410E].ExpectWrite(0xfa);  // HDMITX_DWC_CSC_COEF_B3_MSB
  (*mock_mmio_)[0x410F].ExpectWrite(0xcc);  // HDMITX_DWC_CSC_COEF_B3_LSB
  (*mock_mmio_)[0x4110].ExpectWrite(0x08);  // HDMITX_DWC_CSC_COEF_B4_MSB
  (*mock_mmio_)[0x4111].ExpectWrite(0x00);  // HDMITX_DWC_CSC_COEF_B4_LSB
  (*mock_mmio_)[0x4112].ExpectWrite(0xea);  // HDMITX_DWC_CSC_COEF_C1_MSB
  (*mock_mmio_)[0x4113].ExpectWrite(0xcd);  // HDMITX_DWC_CSC_COEF_C1_LSB
  (*mock_mmio_)[0x4114].ExpectWrite(0xf5);  // HDMITX_DWC_CSC_COEF_C2_MSB
  (*mock_mmio_)[0x4115].ExpectWrite(0x33);  // HDMITX_DWC_CSC_COEF_C2_LSB
  (*mock_mmio_)[0x4116].ExpectWrite(0x20);  // HDMITX_DWC_CSC_COEF_C3_MSB
  (*mock_mmio_)[0x4117].ExpectWrite(0x00);  // HDMITX_DWC_CSC_COEF_C3_LSB
  (*mock_mmio_)[0x4118].ExpectWrite(0x08);  // HDMITX_DWC_CSC_COEF_C4_MSB
  (*mock_mmio_)[0x4119].ExpectWrite(0x00);  // HDMITX_DWC_CSC_COEF_C4_LSB

  (*mock_mmio_)[0x4101].ExpectWrite(0x50);  // HDMITX_DWC_CSC_COEF_C4_LSB
  // ConfigCsc end

  (*mock_mmio_)[0x0801].ExpectWrite(0x00);  // HDMITX_DWC_VP_PR_CD

  (*mock_mmio_)[0x0802].ExpectWrite(0x00);  // HDMITX_DWC_VP_STUFF

  (*mock_mmio_)[0x0803].ExpectWrite(0x00);  // HDMITX_DWC_VP_REMAP

  (*mock_mmio_)[0x0804].ExpectWrite(0x46);  // HDMITX_DWC_VP_CONF

  (*mock_mmio_)[0x0807].ExpectWrite(0xff);  // HDMITX_DWC_VP_MASK

  (*mock_mmio_)[0x1000].ExpectWrite(0xf8);  // HDMITX_DWC_FC_INVIDCONF

  (*mock_mmio_)[0x1001].ExpectWrite(24);  // HDMITX_DWC_FC_INHACTV0
  (*mock_mmio_)[0x1002].ExpectWrite(0);   // HDMITX_DWC_FC_INHACTV1

  (*mock_mmio_)[0x1003].ExpectWrite(93);  // HDMITX_DWC_FC_INHBLANK0
  (*mock_mmio_)[0x1004].ExpectWrite(0);   // HDMITX_DWC_FC_INHBLANK1

  (*mock_mmio_)[0x1005].ExpectWrite(75);  // HDMITX_DWC_FC_INVACTV0
  (*mock_mmio_)[0x1006].ExpectWrite(0);   // HDMITX_DWC_FC_INVACTV1

  (*mock_mmio_)[0x1007].ExpectWrite(83);  // HDMITX_DWC_FC_INVBLANK

  (*mock_mmio_)[0x1008].ExpectWrite(15);  // HDMITX_DWC_FC_HSYNCINDELAY0
  (*mock_mmio_)[0x1009].ExpectWrite(0);   // HDMITX_DWC_FC_HSYNCINDELAY1

  (*mock_mmio_)[0x100A].ExpectWrite(50);  // HDMITX_DWC_FC_HSYNCINWIDTH0
  (*mock_mmio_)[0x100B].ExpectWrite(0);   // HDMITX_DWC_FC_HSYNCINWIDTH1

  (*mock_mmio_)[0x100C].ExpectWrite(104);  // HDMITX_DWC_FC_VSYNCINDELAY

  (*mock_mmio_)[0x100D].ExpectWrite(49);  // HDMITX_DWC_FC_VSYNCINWIDTH

  (*mock_mmio_)[0x1011].ExpectWrite(12);  // HDMITX_DWC_FC_CTRLDUR

  (*mock_mmio_)[0x1012].ExpectWrite(32);  // HDMITX_DWC_FC_EXCTRLDUR

  (*mock_mmio_)[0x1013].ExpectWrite(1);  // HDMITX_DWC_FC_EXCTRLSPAC

  (*mock_mmio_)[0x1018].ExpectWrite(1);  // HDMITX_DWC_FC_GCP

  (*mock_mmio_)[0x1019].ExpectWrite(0x42);  // HDMITX_DWC_FC_AVICONF0

  (*mock_mmio_)[0x101A].ExpectWrite(0x48);  // HDMITX_DWC_FC_AVICONF1

  (*mock_mmio_)[0x101B].ExpectWrite(0x0);  // HDMITX_DWC_FC_AVICONF2

  (*mock_mmio_)[0x1017].ExpectWrite(0x0);  // HDMITX_DWC_FC_AVICONF3

  (*mock_mmio_)[0x10E8].ExpectWrite(0x0);  // HDMITX_DWC_FC_ACTSPC_HDLR_CFG

  (*mock_mmio_)[0x10E9].ExpectWrite(75);  // HDMITX_DWC_FC_INVACT_2D_0
  (*mock_mmio_)[0x10EA].ExpectWrite(0);   // HDMITX_DWC_FC_INVACT_2D_1

  (*mock_mmio_)[0x10D2].ExpectWrite(0xe7);  // HDMITX_DWC_FC_MASK0
  (*mock_mmio_)[0x10D6].ExpectWrite(0xfb);  // HDMITX_DWC_FC_MASK1
  (*mock_mmio_)[0x10DA].ExpectWrite(0x3);   // HDMITX_DWC_FC_MASK2

  (*mock_mmio_)[0x10E0].ExpectWrite(0x10);  // HDMITX_DWC_FC_PRCONF

  (*mock_mmio_)[0x0100].ExpectWrite(0xff);  // HDMITX_DWC_IH_FC_STAT0
  (*mock_mmio_)[0x0101].ExpectWrite(0xff);  // HDMITX_DWC_IH_FC_STAT1
  (*mock_mmio_)[0x0102].ExpectWrite(0xff);  // HDMITX_DWC_IH_FC_STAT2
  (*mock_mmio_)[0x0103].ExpectWrite(0xff);  // HDMITX_DWC_IH_AS_STAT0
  (*mock_mmio_)[0x0104].ExpectWrite(0xff);  // HDMITX_DWC_IH_PHY_STAT0
  (*mock_mmio_)[0x0105].ExpectWrite(0xff);  // HDMITX_DWC_IH_I2CM_STAT0
  (*mock_mmio_)[0x0106].ExpectWrite(0xff);  // HDMITX_DWC_IH_CEC_STAT0
  (*mock_mmio_)[0x0107].ExpectWrite(0xff);  // HDMITX_DWC_IH_VP_STAT0
  (*mock_mmio_)[0x0108].ExpectWrite(0xff);  // HDMITX_DWC_IH_I2CMPHY_STAT0
  (*mock_mmio_)[0x5006].ExpectWrite(0xff);  // HDMITX_DWC_A_APIINTCLR
  (*mock_mmio_)[0x790D].ExpectWrite(0xff);  // HDMITX_DWC_HDCP22REG_STAT

  hdmi_dw_->ConfigHdmitx(mode, p);
}

TEST_F(HdmiDwTest, SetupInterruptsTest) {
  (*mock_mmio_)[0x0180].ExpectWrite(0xff);  // HDMITX_DWC_IH_MUTE_FC_STAT0
  (*mock_mmio_)[0x0181].ExpectWrite(0xff);  // HDMITX_DWC_IH_MUTE_FC_STAT1
  (*mock_mmio_)[0x0182].ExpectWrite(0x3);   // HDMITX_DWC_IH_MUTE_FC_STAT2

  (*mock_mmio_)[0x0183].ExpectWrite(0x7);  // HDMITX_DWC_IH_MUTE_AS_STAT0

  (*mock_mmio_)[0x0184].ExpectWrite(0x3f);  // HDMITX_DWC_IH_MUTE_PHY_STAT0

  (*mock_mmio_)[0x0185].ExpectWrite(1 << 1);  // HDMITX_DWC_IH_MUTE_I2CM_STAT0

  (*mock_mmio_)[0x0186].ExpectWrite(0x0);  // HDMITX_DWC_IH_MUTE_CEC_STAT0

  (*mock_mmio_)[0x0187].ExpectWrite(0xff);  // HDMITX_DWC_IH_MUTE_VP_STAT0

  (*mock_mmio_)[0x0188].ExpectWrite(0x03);  // HDMITX_DWC_IH_MUTE_I2CMPHY_STAT0

  (*mock_mmio_)[0x01FF].ExpectWrite(0x00);  // HDMITX_DWC_IH_MUTE

  hdmi_dw_->SetupInterrupts();
}

TEST_F(HdmiDwTest, ResetTest) {
  (*mock_mmio_)[0x4002].ExpectWrite(0x00).ExpectWrite(0x7d);  // HDMITX_DWC_MC_SWRSTZREQ
  (*mock_mmio_)[0x100D].ExpectRead(0x41).ExpectWrite(0x41);   // HDMITX_DWC_FC_VSYNCINWIDTH

  (*mock_mmio_)[0x4001].ExpectWrite(0x00);  // HDMITX_DWC_MC_CLKDIS

  hdmi_dw_->Reset();
}

TEST_F(HdmiDwTest, SetupScdcTest) {
  // is4k = true
  ExpectScdcRead(0x1, 0);
  ExpectScdcWrite(0x2, 0x1);
  ExpectScdcWrite(0x2, 0x1);

  ExpectScdcWrite(0x20, 0x3);
  ExpectScdcWrite(0x20, 0x3);

  hdmi_dw_->SetupScdc(true);

  // is4k = false
  ExpectScdcRead(0x1, 0);
  ExpectScdcWrite(0x2, 0x1);
  ExpectScdcWrite(0x2, 0x1);

  ExpectScdcWrite(0x20, 0x0);
  ExpectScdcWrite(0x20, 0x0);

  hdmi_dw_->SetupScdc(false);
}

TEST_F(HdmiDwTest, ResetFcTest) {
  (*mock_mmio_)[0x1000].ExpectRead(0xff).ExpectWrite(0xf7).ExpectRead(0x00).ExpectWrite(
      0x08);  // HDMITX_DWC_FC_INVIDCONF

  hdmi_dw_->ResetFc();
}

TEST_F(HdmiDwTest, SetFcScramblerCtrlTest) {
  // is4k = true
  (*mock_mmio_)[0x10E1].ExpectRead(0x00).ExpectWrite(0x01);  // HDMITX_DWC_FC_SCRAMBLER_CTRL

  hdmi_dw_->SetFcScramblerCtrl(true);

  // is4k = false
  (*mock_mmio_)[0x10E1].ExpectWrite(0x00);  // HDMITX_DWC_FC_SCRAMBLER_CTRL

  hdmi_dw_->SetFcScramblerCtrl(false);
}

}  // namespace hdmi_dw
