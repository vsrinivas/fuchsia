// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dsi-mt.h"

#include <fuchsia/sysmem/c/fidl.h>

#include <ddk/binding.h>
#include <ddk/metadata.h>
#include <ddk/metadata/display.h>
#include <ddk/platform-defs.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>

#include "mt-dsi-reg.h"

namespace dsi_mt {

#define MAX(a, b) (a > b ? a : b)

namespace {
constexpr uint32_t kWMemCommand = 0x3C;
constexpr uint32_t kBusyTimeout = 500000;  // from vendor
constexpr uint32_t kReadTimeout = 20;      // Unit: ms
constexpr uint32_t kMaxPayloadLength = 64;
constexpr uint32_t kMaxReadResponse = 12;

// MIPI-PHY Related constants based on spec
constexpr uint32_t kTrailOffset = 0xa;
constexpr uint32_t kHsTrailParam = 0x64;
constexpr uint32_t kHsPrepParam = 0x40;
constexpr uint32_t kHsPrepUiMultiplier = 0x5;
constexpr uint32_t kHsZeroParam = 0xC8;
constexpr uint32_t kHsZeroUiMultiplier = 0x0a;
constexpr uint32_t kLpxParam = 0x50;
constexpr uint32_t kHsExitParam = 0x3c;
constexpr uint32_t kHsExitUiMultiplier = 0x80;
constexpr uint32_t kTaGetLpxMultiplier = 0x5;
constexpr uint32_t kTaSureLpxMultiplier = 0x3;
constexpr uint32_t kTaSureLpxDivider = 0x2;
constexpr uint32_t kTaGoMultiplier = 0x4;
constexpr uint32_t kClkTrailParam = 0x64;
constexpr uint32_t kContDet = 0;
constexpr uint32_t kClkZeroParam = 0x190;
constexpr uint32_t kClkPrepParam = 0x40;
constexpr uint32_t kClkExitLpxMultiplier = 0x2;
constexpr uint32_t kClkPostParam = 0x3c;
constexpr uint32_t kClkPostUiMultiplier = 0x80;

enum {
  TYPE_SHORT = 0,
  TYPE_LONG = 2,
};

}  // namespace

zx_status_t DsiMt::DsiImplWriteReg(uint32_t reg, uint32_t val) {
  // TODO(payamm): Verify register offset is valid
  dsi_mmio_->Write32(val, reg);
  return ZX_OK;
}
zx_status_t DsiMt::DsiImplReadReg(uint32_t reg, uint32_t* val) {
  // TODO(payamm): Verify register offset is valid
  *val = dsi_mmio_->Read32(reg);
  return ZX_OK;
}

zx_status_t DsiMt::DsiImplEnableBist(uint32_t pattern) {
  // bist enable
  DsiImplSetMode(DSI_MODE_VIDEO);
  DSI_INFO("Enabling BIST\n");
  DsiBistPatternReg::Get().FromValue(pattern).WriteTo(&(*dsi_mmio_));
  DsiBistConReg::Get().ReadFrom(&(*dsi_mmio_)).set_sel_pat_mode(1).WriteTo(&(*dsi_mmio_));
  StartDsi();
  return ZX_OK;
}

zx_status_t DsiMt::GetColorCode(color_code_t c, uint8_t& code) {
  zx_status_t status = ZX_OK;
  switch (c) {
    case COLOR_CODE_PACKED_16BIT_565:
      code = 0;
      break;
    case COLOR_CODE_PACKED_18BIT_666:
      code = 1;
      break;
    case COLOR_CODE_LOOSE_24BIT_666:
      code = 2;
      break;
    case COLOR_CODE_PACKED_24BIT_888:
      code = 3;
      break;
    default:
      status = ZX_ERR_INVALID_ARGS;
      break;
  }
  return status;
}

zx_status_t DsiMt::GetVideoMode(video_mode_t v, uint8_t& mode) {
  zx_status_t status = ZX_OK;
  switch (v) {
    case VIDEO_MODE_NON_BURST_PULSE:
      mode = 1;
      break;
    case VIDEO_MODE_NON_BURST_EVENT:
      mode = 2;
      break;
    case VIDEO_MODE_BURST:
      mode = 3;
      break;
    default:
      status = ZX_ERR_INVALID_ARGS;
  }
  return status;
}

zx_status_t DsiMt::DsiImplConfig(const dsi_config_t* dsi_config) {
  const display_setting_t disp_setting = dsi_config->display_setting;

  // Calculated ui and cycle_time needed for phy configuration
  ui_ = 1000 / (disp_setting.lcd_clock * 2) + 0x01;
  cycle_time_ = 8000 / (disp_setting.lcd_clock * 2) + 0x01;

  // Make sure we support the color code
  uint8_t code;
  zx_status_t status = GetColorCode(dsi_config->color_coding, code);
  if (status != ZX_OK) {
    DSI_ERROR("Invalid/Unsupported color coding %d\n", status);
    return status;
  }
  uint8_t video_mode;
  status = GetVideoMode(dsi_config->video_mode_type, video_mode);
  if (status != ZX_OK) {
    DSI_ERROR("Invalid/Unsupported video mode\n");
    return status;
  }

  // TODO(payamm): We only support sync-pulse mode.
  if (dsi_config->video_mode_type != VIDEO_MODE_NON_BURST_PULSE) {
    DSI_ERROR("Video Mode: Non-Burst pulse supported only\n");
    // TODO(payamm): Add burst mode support
    return status;
  }

  // enable highspeed mode in command mode
  DsiPhyLcconReg::Get().ReadFrom(&(*dsi_mmio_)).set_lc_hstx_en(1).WriteTo(&(*dsi_mmio_));

  // Setup TXRX Control as follows:
  // Set Virtual Channel to 0, disable end of transmission packet, disable null packet in bllp,
  // set max_return_size to zero, disable hs clock lane non-continuous mode and configures the
  // correct number of lanes.
  DsiTxRxCtrlReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vc_num(0)
      .set_hstx_dis_eot(0)
      .set_hstx_bllp_en(0)
      .set_hstx_cklp_en(0)
      .set_lane_num((1 << disp_setting.lane_num) - 1)
      .WriteTo(&(*dsi_mmio_));

  // Set Read/Write memory continue command. This is used for Type-1 FrameBuffer Write
  DsiMemContReg::Get().ReadFrom(&(*dsi_mmio_)).set_rwmem_cont(kWMemCommand).WriteTo(&(*dsi_mmio_));

  // Set pixel stream type
  // TODO(payamm): Confirm width_ == h_active
  uint8_t bpp = (dsi_config->color_coding == COLOR_CODE_PACKED_16BIT_565) ? 2 : 3;
  DsiPsCtrlReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_ps_wc(disp_setting.h_active * bpp)
      .set_ps_sel(code)
      .WriteTo(&(*dsi_mmio_));

  // Setup vertical parameters
  DsiVsaNlReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vsa(disp_setting.vsync_width)
      .WriteTo(&(*dsi_mmio_));

  DsiVbpNlReg::Get().ReadFrom(&(*dsi_mmio_)).set_vbp(disp_setting.vsync_bp).WriteTo(&(*dsi_mmio_));

  DsiVfpNlReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vfp(disp_setting.v_period - disp_setting.v_active - disp_setting.vsync_bp -
               disp_setting.vsync_width)
      .WriteTo(&(*dsi_mmio_));

  DsiVactNlReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_vact(disp_setting.v_active)
      .WriteTo(&(*dsi_mmio_));

  // The subtractions at the end of the calculations below are slight adjustments
  // needed to leave some space for HS prep time due to non-continuous data lane transmission
  // The numbers come from MT8167s spec
  uint32_t h_fp = disp_setting.h_period - disp_setting.h_active - disp_setting.hsync_bp -
                  disp_setting.hsync_width;
  uint32_t hsync_width_byte = ALIGN(disp_setting.hsync_width * bpp - 10, 4);

  uint32_t h_bp_byte;
  if (dsi_config->video_mode_type == VIDEO_MODE_BURST) {
    h_bp_byte = ALIGN((disp_setting.hsync_bp + disp_setting.hsync_width) * bpp - 10, 4);
    hsync_width_byte = ALIGN(disp_setting.hsync_width * bpp - 4, 4);
  } else {
    h_bp_byte = ALIGN(disp_setting.hsync_bp * bpp - 10, 4);
  }

  uint32_t h_fp_byte = ALIGN(h_fp * bpp - 12, 4);

  DsiHsaWcReg::Get().ReadFrom(&(*dsi_mmio_)).set_hsa(hsync_width_byte).WriteTo(&(*dsi_mmio_));
  DsiHbpWcReg::Get().ReadFrom(&(*dsi_mmio_)).set_hbp(h_bp_byte).WriteTo(&(*dsi_mmio_));
  DsiHfpWcReg::Get().ReadFrom(&(*dsi_mmio_)).set_hfp(h_fp_byte).WriteTo(&(*dsi_mmio_));

  // Set horizontal blanking to 0 since we do not operate in burst mode
  // TODO(payamm): Revisit if Burst mode is added
  DsiBllpWcReg::Get().ReadFrom(&(*dsi_mmio_)).set_bllp(0).WriteTo(&(*dsi_mmio_));

  // Enable sending commands in video mode. We set this register up to only send commands
  // (i.e. short) during VFP period. (TODO: try to really understand this feature)
  DsiVmCmdConReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_ts_vfp_en(1)
      .set_vm_cmd_en(1)
      .WriteTo(&(*dsi_mmio_));
  return ZX_OK;
}

void DsiMt::DsiImplPhyPowerUp() {
  // Configure TimeCon0 Register which includes hs_trail, hs_zero, hs_prep and lpx
  // hs_trail: Time that the transmitter drives the flipped differential state after
  //           last payload data bit of a HS transmission burst
  // hs_prep:  Time that hte transmisster drives the Data Lane LP-00 line state
  //           immediately before the HS-0 line state starting the HS transmission
  // hs_zero:  Time that the transmitter drives the hs-0 state prior to transmitting the
  //           sync sequence
  // lpx:      Transmitted length of any low-power state period
  uint32_t hs_trail = MAX(NsToCycle(kHsTrailParam), 1) + kTrailOffset;
  uint32_t hs_prep = MAX(NsToCycle(kHsPrepParam + kHsPrepUiMultiplier * ui_), 1);
  uint32_t hs_zero = NsToCycle(kHsZeroParam + kHsZeroUiMultiplier * ui_);
  // make sure hs_zero does not exceed hs_prep
  if (hs_zero > hs_prep) {
    hs_zero -= hs_prep;
  }
  uint32_t lpx = MAX(NsToCycle(kLpxParam), 1);

  DsiPhyTimeCon0Reg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_hs_trail(hs_trail)
      .set_hs_zero(hs_zero)
      .set_hs_prep(hs_prep)
      .set_lpx(lpx)
      .WriteTo(&(*dsi_mmio_));

  // Configure TimeCon1 Register which includes hs_exit, ta_get, ta_sure and ta_go
  // hs_exit: time that the transmitter drives LP-11 following a HS burst
  // ta_get:  Time that the new transmitter drives the bridge state (lp-00) after accepting
  //          control during a link turnaround
  // ta_sure: Time that the new transmitter waits after the lp-10 state before transmitting
  //          the bridge state (lp-00) during a link turnaround
  // ta_go:   Time that the transmitter drives the bridge state (lp-00) before releasing control
  //          during a link turnaround
  uint32_t ta_get = kTaGetLpxMultiplier * lpx;
  uint32_t ta_sure = kTaSureLpxMultiplier * lpx / kTaSureLpxDivider;
  uint32_t ta_go = kTaGoMultiplier * lpx;
  uint32_t hs_exit = NsToCycle(kHsExitParam + kHsExitUiMultiplier * ui_);

  DsiPhyTimeCon1Reg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_hs_exit(hs_exit)
      .set_ta_get(ta_get)
      .set_ta_sure(ta_sure)
      .set_ta_go(ta_go)
      .WriteTo(&(*dsi_mmio_));

  // Configure TimeCon2 Register which includes clk_trail, clk_zero and cont_det
  // clk_trail: Time that the transmitter drives the hs-0 state after the last payload clock bit
  //            of a hs transmission burst
  // clk_zero:  Time that the transmitter drives the hs-0 state prior to starting the clock
  // cont_det:  Not sure. Set to 0
  uint32_t clk_trail = NsToCycle(kClkTrailParam) + kTrailOffset;
  uint32_t clk_zero = NsToCycle(kClkZeroParam);

  DsiPhyTimeCon2Reg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_clk_trail(clk_trail)
      .set_clk_zero(clk_zero)
      .set_cont_det(kContDet)
      .WriteTo(&(*dsi_mmio_));

  // Configure TimeCon3 Register which includes clk_exit, clk_post and clk_prep
  // clk_post: Time that the transmitter continues to send HS clock after the last associated
  //           Data Lane has transitioned to LP mode
  // clk_prep: Time that the transmitter drives the clock lane lp-00 line state immidiately
  //           before the hs-0 line state starting the hs transmission
  uint32_t clk_prep = MAX(NsToCycle(kClkPrepParam), 1);
  uint32_t clk_exit = kClkExitLpxMultiplier * lpx;
  uint32_t clk_post = NsToCycle(kClkPostParam + kClkPostUiMultiplier * ui_);

  DsiPhyTimeCon3Reg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_clk_exit(clk_exit)
      .set_clk_post(clk_post)
      .set_clk_prep(clk_prep)
      .WriteTo(&(*dsi_mmio_));
}

// MT Command Queue looks something like this: <Data1><Data0><Data ID><Config>
// where Config is: [7:6rsv][5TE][4 CL][3 HS][2 BTA][1:0 Type]
// Where Type is 00: Short read/write, 10: Generic Long and 01/03: Framebuffer R/W which
// are not supported in this driver
//
zx_status_t DsiMt::DsiImplSendCmd(const mipi_dsi_cmd_t* cmd_list, size_t cmd_count) {
  zx_status_t status = ZX_OK;

  for (size_t i = 0; i < cmd_count && status == ZX_OK; i++) {
    mipi_dsi_cmd_t cmd = cmd_list[i];

    switch (cmd.dsi_data_type) {
      case MIPI_DSI_DT_GEN_SHORT_WRITE_0:
      case MIPI_DSI_DT_GEN_SHORT_WRITE_1:
      case MIPI_DSI_DT_GEN_SHORT_WRITE_2:
      case MIPI_DSI_DT_GEN_LONG_WRITE:
      case MIPI_DSI_DT_DCS_LONG_WRITE:
      case MIPI_DSI_DT_DCS_SHORT_WRITE_0:
      case MIPI_DSI_DT_DCS_SHORT_WRITE_1:
        status = Write(cmd);
        break;
      case MIPI_DSI_DT_GEN_SHORT_READ_0:
      case MIPI_DSI_DT_GEN_SHORT_READ_1:
      case MIPI_DSI_DT_GEN_SHORT_READ_2:
      case MIPI_DSI_DT_DCS_READ_0:
        status = Read(cmd);
        break;
      default:
        DSI_ERROR("Unsupported/Invalid DSI Command type %d\n", cmd.dsi_data_type);
        status = ZX_ERR_INVALID_ARGS;
    }

    if (status != ZX_OK) {
      DSI_ERROR("Something went wrong in sending command\n");
      DsiImplPrintDsiRegisters();
      break;
    }
  }
  return status;
}

void DsiMt::DsiImplSetMode(dsi_mode_t mode) {
  uint8_t dsi_mode = (mode == DSI_MODE_COMMAND) ? 0 : 1;
  auto current_mode = DsiModeCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).mode_con();

  if (dsi_mode == current_mode) {
    DSI_INFO("No need to change mode\n");
    // return;
  }

  DsiModeCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).set_mode_con(dsi_mode).WriteTo(&(*dsi_mmio_));
}

void DsiMt::DsiImplPowerUp() {
  // TODO(payamm): Should we toggle reset here before powering up?
  DsiComCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).set_dsi_en(1).WriteTo(&(*dsi_mmio_));
}

void DsiMt::DsiImplPowerDown() {
  // Disable highspeed mode
  DsiPhyLcconReg::Get().ReadFrom(&(*dsi_mmio_)).set_lc_hstx_en(0).WriteTo(&(*dsi_mmio_));

  // clear lane_num
  DsiTxRxCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).set_lane_num(0).WriteTo(&(*dsi_mmio_));

  DsiImplReset();
  DsiComCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).set_dsi_en(0).WriteTo(&(*dsi_mmio_));
}

bool DsiMt::DsiImplIsPoweredUp() {
  return (DsiComCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).dsi_en() == 1);
}

void DsiMt::DsiImplReset() {
  DsiComCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).set_dsi_reset(1).WriteTo(&(*dsi_mmio_));

  zx_nanosleep(zx_deadline_after(ZX_USEC(50)));

  DsiComCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).set_dsi_reset(0).WriteTo(&(*dsi_mmio_));
}

void DsiMt::DsiImplPrintDsiRegisters() {
  zxlogf(INFO, "Dumping DSI MT Registers:\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "DsiStartReg = 0x%x\n", DsiStartReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStaReg = 0x%x\n", DsiStaReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiIntEnReg = 0x%x\n", DsiIntEnReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiIntStaReg = 0x%x\n", DsiIntStaReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiComCtrlReg = 0x%x\n", DsiComCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiModeCtrlReg = 0x%x\n",
         DsiModeCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiTxRxCtrlReg = 0x%x\n",
         DsiTxRxCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiPsCtrlReg = 0x%x\n", DsiPsCtrlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiVsaNlReg = 0x%x\n", DsiVsaNlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiVbpNlReg = 0x%x\n", DsiVbpNlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiVfpNlReg = 0x%x\n", DsiVfpNlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiVactNlReg = 0x%x\n", DsiVactNlReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiHsaWcReg = 0x%x\n", DsiHsaWcReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiHbpWcReg = 0x%x\n", DsiHbpWcReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiHfpWcReg = 0x%x\n", DsiHfpWcReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiBllpWcReg = 0x%x\n", DsiBllpWcReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiCmdqSizeReg = 0x%x\n",
         DsiCmdqSizeReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiHstxCklWcReg = 0x%x\n",
         DsiHstxCklWcReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiRxData03Reg = 0x%x\n",
         DsiRxData03Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiRxData47Reg = 0x%x\n",
         DsiRxData47Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiRxData8bReg = 0x%x\n",
         DsiRxData8bReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiRxDataCReg = 0x%x\n", DsiRxDataCReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiRackReg = 0x%x\n", DsiRackReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiTrigStaReg = 0x%x\n", DsiTrigStaReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiMemContReg = 0x%x\n", DsiMemContReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiFrmBcReg = 0x%x\n", DsiFrmBcReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiPhyLcpatReg = 0x%x\n",
         DsiPhyLcpatReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiPhyLcconReg = 0x%x\n",
         DsiPhyLcconReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiPhyLd0ConReg = 0x%x\n",
         DsiPhyLd0ConReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiPhyTimeCon0Reg = 0x%x\n",
         DsiPhyTimeCon0Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiPhyTimeCon1Reg = 0x%x\n",
         DsiPhyTimeCon1Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiPhyTimeCon2Reg = 0x%x\n",
         DsiPhyTimeCon2Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiPhyTimeCon3Reg = 0x%x\n",
         DsiPhyTimeCon3Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiPhyTimeCon4Reg = 0x%x\n",
         DsiPhyTimeCon4Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiVmCmdConReg = 0x%x\n",
         DsiVmCmdConReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiVmCmdData0Reg = 0x%x\n",
         DsiVmCmdData0Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiVmCmdData4Reg = 0x%x\n",
         DsiVmCmdData4Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiVmCmdData8Reg = 0x%x\n",
         DsiVmCmdData8Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiVmCmdDataCReg = 0x%x\n",
         DsiVmCmdDataCReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiCksmOutReg = 0x%x\n", DsiCksmOutReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg0Reg = 0x%x\n",
         DsiStateDbg0Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg1Reg = 0x%x\n",
         DsiStateDbg1Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg2Reg = 0x%x\n",
         DsiStateDbg2Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg3Reg = 0x%x\n",
         DsiStateDbg3Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg4Reg = 0x%x\n",
         DsiStateDbg4Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg5Reg = 0x%x\n",
         DsiStateDbg5Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg6Reg = 0x%x\n",
         DsiStateDbg6Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg7Reg = 0x%x\n",
         DsiStateDbg7Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg8Reg = 0x%x\n",
         DsiStateDbg8Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiStateDbg9Reg = 0x%x\n",
         DsiStateDbg9Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiDebugSelReg = 0x%x\n",
         DsiDebugSelReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiBistPatternReg = 0x%x\n",
         DsiBistPatternReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "DsiBistConReg = 0x%x\n", DsiBistConReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value());
  zxlogf(INFO, "######################\n\n");
}

void DsiMt::StartDsi() {
  // Toggle Start bit
  DsiStartReg::Get().ReadFrom(&(*dsi_mmio_)).set_dsi_start(0).WriteTo(&(*dsi_mmio_));
  DsiStartReg::Get().ReadFrom(&(*dsi_mmio_)).set_dsi_start(1).WriteTo(&(*dsi_mmio_));
}

zx_status_t DsiMt::WaitForIdle() {
  int timeout = kBusyTimeout;
  auto stat_reg = DsiIntStaReg::Get();

  while (stat_reg.ReadFrom(&(*dsi_mmio_)).dsi_busy() && timeout--) {
    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
  }

  if (timeout <= 0) {
    DSI_ERROR("Timeout! DSI remains busy\n");
    // TODO(payamm): perform reset and dump registers
    return ZX_ERR_TIMED_OUT;
  }

  // clear register
  stat_reg.FromValue(0).WriteTo(&(*dsi_mmio_));
  return ZX_OK;
}

zx_status_t DsiMt::WaitForRxReady() {
  int timeout = kReadTimeout;
  auto stat_reg = DsiIntStaReg::Get();

  while (!stat_reg.ReadFrom(&(*dsi_mmio_)).lprx_rd_rdy() && timeout--) {
    zx_nanosleep(zx_deadline_after(ZX_MSEC(1)));
  }

  if (timeout <= 0) {
    DSI_ERROR("Timeout! DSI remains busy\n");
    // TODO(payamm): perform reset and dump registers
    return ZX_ERR_TIMED_OUT;
  }

  // clear register
  stat_reg.FromValue(0).WriteTo(&(*dsi_mmio_));
  return ZX_OK;
}

zx_status_t DsiMt::Read(const mipi_dsi_cmd_t& cmd) {
  if ((cmd.rsp_data_list == nullptr) || (cmd.pld_data_count > 2) ||
      (cmd.pld_data_count > 0 && cmd.pld_data_list == nullptr)) {
    DSI_ERROR("Invalid read command packet\n");
    return ZX_ERR_INVALID_ARGS;
  }

  if (cmd.rsp_data_count > kMaxReadResponse) {
    DSI_ERROR("Expected Read exceeds %d\n", kMaxReadResponse);
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Make sure DSI is not busy
  zx_status_t status = WaitForIdle();
  if (status != ZX_OK) {
    DSI_ERROR("Could not send command (%d)\n", status);
    return status;
  }

  auto cmdq_reg = CmdQReg::Get(0).FromValue(0);

  // Check whether max return packet size should be set
  if (cmd.flags & MIPI_DSI_CMD_FLAGS_SET_MAX) {
    // We will set the max return size as rlen
    cmdq_reg.set_data_id(MIPI_DSI_DT_SET_MAX_RET_PKT);
    cmdq_reg.set_type(TYPE_SHORT);
    cmdq_reg.set_data_0(static_cast<uint32_t>(cmd.rsp_data_count) & 0xFF);
    cmdq_reg.set_data_1((static_cast<uint32_t>(cmd.rsp_data_count) >> 8) & 0xFF);
    cmdq_reg.WriteTo(&(*dsi_mmio_));
    DsiCmdqSizeReg::Get().FromValue(0).set_cmdq_reg_size(1).WriteTo(&(*dsi_mmio_));
    StartDsi();
    status = WaitForIdle();
    if (status != ZX_OK) {
      DSI_ERROR("Command did not complete (%d)\n", status);
      return status;
    }
  }

  // Make sure DSI is not busy
  status = WaitForIdle();
  if (status != ZX_OK) {
    DSI_ERROR("Could not send command (%d)\n", status);
    return status;
  }

  // Setup the read packet
  cmdq_reg.set_reg_value(0);
  cmdq_reg.set_type(TYPE_SHORT);
  cmdq_reg.set_data_id(cmd.dsi_data_type);
  cmdq_reg.set_bta(1);
  if (cmd.pld_data_count >= 1) {
    cmdq_reg.set_data_0(cmd.pld_data_list[0]);
  }
  if (cmd.pld_data_count == 2) {
    cmdq_reg.set_data_1(cmd.pld_data_list[1]);
  }
  cmdq_reg.WriteTo(&(*dsi_mmio_));
  DsiCmdqSizeReg::Get().FromValue(0).set_cmdq_reg_size(1).WriteTo(&(*dsi_mmio_));

  DsiRackReg::Get().ReadFrom(&(*dsi_mmio_)).set_rack(1).WriteTo(&(*dsi_mmio_));

  DsiIntStaReg::Get()
      .ReadFrom(&(*dsi_mmio_))
      .set_lprx_rd_rdy(1)
      .set_cmd_done(1)
      .WriteTo(&(*dsi_mmio_));

  StartDsi();

  // Wait for read to finish
  status = WaitForRxReady();
  if (status != ZX_OK) {
    DSI_ERROR("Read not completed\n");
    return status;
  }

  DsiRackReg::Get().ReadFrom(&(*dsi_mmio_)).set_rack(1).WriteTo(&(*dsi_mmio_));

  // store a local copy of the response registers.
  uint32_t read_buf[3];
  read_buf[0] = DsiRxData47Reg::Get().ReadFrom(&(*dsi_mmio_)).reg_value();
  read_buf[1] = DsiRxData8bReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value();
  read_buf[2] = DsiRxDataCReg::Get().ReadFrom(&(*dsi_mmio_)).reg_value();

  // Determine response type first
  auto rx_data_reg03 = DsiRxData03Reg::Get().ReadFrom(&(*dsi_mmio_));
  switch (rx_data_reg03.byte0()) {
    case MIPI_DSI_RSP_GEN_SHORT_1:
    case MIPI_DSI_RSP_GEN_SHORT_2:
    case MIPI_DSI_RSP_DCS_SHORT_1:
    case MIPI_DSI_RSP_DCS_SHORT_2:
      // For short response, byte1 and 2 contain the returned value
      if (cmd.rsp_data_count >= 1) {
        cmd.rsp_data_list[0] = static_cast<uint8_t>(rx_data_reg03.byte1());
      }
      if (cmd.rsp_data_count == 2) {
        cmd.rsp_data_list[1] = static_cast<uint8_t>(rx_data_reg03.byte2());
      }
      break;
    case MIPI_DSI_RSP_GEN_LONG:
    case MIPI_DSI_RSP_DCS_LONG: {
      // For long responses, <byte2><byte1> contains the response bytes
      size_t rsp_count = rx_data_reg03.byte2() << 8 | rx_data_reg03.byte1();
      size_t actual_read = (rsp_count < cmd.rsp_data_count) ? rsp_count : cmd.rsp_data_count;
      memcpy(cmd.rsp_data_list, read_buf, actual_read);
    } break;
    default:
      DSI_ERROR("Invalid Response Type\n");
      break;
  }

  return ZX_OK;
}

zx_status_t DsiMt::Write(const mipi_dsi_cmd_t& cmd) {
  if (cmd.pld_data_count > 0 && cmd.pld_data_list == nullptr) {
    DSI_ERROR("Invalid write command packet\n");
    return ZX_ERR_INVALID_ARGS;
  }

  if (cmd.pld_data_count > kMaxPayloadLength) {
    DSI_ERROR("Payload length exceeds %d\n", kMaxPayloadLength);
    return ZX_ERR_OUT_OF_RANGE;
  }

  // Make sure DSI is not busy
  zx_status_t status = WaitForIdle();
  if (status != ZX_OK) {
    DSI_ERROR("Could not send command (%d)\n", status);
    return status;
  }

  // Both short and long writes need the first command queue register to setup the
  // outgoing packet. In case of short write, DATA0 and DATA1 contain actual data. In
  // case of long write, DATA0 and DATA1 contain the word count
  if (cmd.pld_data_count > 2) {
    // Long write
    auto cmdq_reg = CmdQReg::Get(0).FromValue(0);
    cmdq_reg.set_type(TYPE_LONG);
    cmdq_reg.set_data_0(static_cast<uint8_t>(cmd.pld_data_count));
    cmdq_reg.set_data_1(0);  // we only support 64Bytes, so WC1 will be zero
    // Setup DataID
    cmdq_reg.set_data_id(cmd.dsi_data_type);
    // At this point, cmd packet is ready. Write it
    cmdq_reg.WriteTo(&(*dsi_mmio_));

    // Write remaining parameters if long write
    uint32_t ts = static_cast<uint8_t>(cmd.pld_data_count);
    uint32_t pld_data_idx = 0;  // payload data index
    uint32_t cmdq_index = 1;    // start from the second queue (first one contains the command)
    // write complete words first
    while (ts >= 4) {
      uint32_t qval =
          cmd.pld_data_list[pld_data_idx + 0] << 0 | cmd.pld_data_list[pld_data_idx + 1] << 8 |
          cmd.pld_data_list[pld_data_idx + 2] << 16 | cmd.pld_data_list[pld_data_idx + 3] << 24;
      auto cmdq_reg2 = CmdQReg::Get(cmdq_index).FromValue(0);
      cmdq_reg2.set_reg_value(qval).WriteTo(&(*dsi_mmio_));
      pld_data_idx += 4;
      cmdq_index++;
      ts -= 4;
    }
    // Write remaining bytes
    if (ts > 0) {
      uint32_t qval = cmd.pld_data_list[pld_data_idx++] << 0;
      if (ts > 1) {
        qval |= cmd.pld_data_list[pld_data_idx++] << 8;
      }
      if (ts > 2) {
        qval |= cmd.pld_data_list[pld_data_idx++] << 16;
      }
      auto cmdq_reg2 = CmdQReg::Get(cmdq_index).FromValue(0);
      cmdq_reg2.set_reg_value(qval).WriteTo(&(*dsi_mmio_));
      cmdq_index++;
    }
    // set command queue size (only 1 entry)
    DsiCmdqSizeReg::Get().FromValue(0).set_cmdq_reg_size(cmdq_index).WriteTo(&(*dsi_mmio_));
  } else {
    // Short write
    auto cmdq_reg = CmdQReg::Get(0).FromValue(0);
    cmdq_reg.set_data_id(cmd.dsi_data_type);
    cmdq_reg.set_type(TYPE_SHORT);
    if (cmd.pld_data_count >= 1) {
      cmdq_reg.set_data_0(cmd.pld_data_list[0]);
    }
    if (cmd.pld_data_count == 2) {
      cmdq_reg.set_data_1(cmd.pld_data_list[1]);
    }
    // At this point, cmd packet is ready. Write it
    cmdq_reg.WriteTo(&(*dsi_mmio_));
    // set command queue size (only 1 entry)
    DsiCmdqSizeReg::Get().FromValue(0).set_cmdq_reg_size(1).WriteTo(&(*dsi_mmio_));
  }

  // At this point, we have written all data into the queue. Let's transmit now

  // Start DSI Engine
  StartDsi();
  // Wait for command to complete
  status = WaitForIdle();
  if (status != ZX_OK) {
    DSI_ERROR("Command did not complete (%d)\n", status);
    return status;
  }
  return status;
}

zx_status_t DsiMt::Bind() {
  zx_status_t status = device_get_protocol(parent_, ZX_PROTOCOL_PDEV, &pdev_proto_);
  if (status != ZX_OK) {
    DSI_ERROR("Could not get parent protocol (%d)\n", status);
    return status;
  }

  // Map DSI registers
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_proto_, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DSI_ERROR("Could not map DSI mmio (%d)\n", status);
    return status;
  }

  dsi_mmio_ = ddk::MmioBuffer(mmio);

  // Obtain display metadata needed to load the proper display driver
  display_driver_t display_info;
  size_t actual;
  status = device_get_metadata(parent_, DEVICE_METADATA_DISPLAY_DEVICE, &display_info,
                               sizeof(display_driver_t), &actual);
  if (status != ZX_OK || actual != sizeof(display_driver_t)) {
    DSI_ERROR("Could not get display driver metadata %d\n", status);
    return status;
  }

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, display_info.vid},
      {BIND_PLATFORM_DEV_PID, 0, display_info.pid},
      {BIND_PLATFORM_DEV_DID, 0, display_info.did},
  };

  status = DdkAdd("mt-dsi", 0, props, countof(props));
  if (status != ZX_OK) {
    DSI_ERROR("could not add device %d\n", status);
  }
  return status;
}

// main bind function called from dev manager
zx_status_t dsi_mt_bind(void* ctx, zx_device_t* parent) {
  fbl::AllocChecker ac;
  auto dev = fbl::make_unique_checked<dsi_mt::DsiMt>(&ac, parent);
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }
  auto status = dev->Bind();
  if (status == ZX_OK) {
    // devmgr is now in charge of the memory for dev
    __UNUSED auto ptr = dev.release();
  }
  return status;
}

static constexpr zx_driver_ops_t dsi_mt_ops = [] {
  zx_driver_ops_t ops = {};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = dsi_mt_bind;
  return ops;
}();

}  // namespace dsi_mt

// clang-format off
ZIRCON_DRIVER_BEGIN(dsi_mt, dsi_mt::dsi_mt_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_MEDIATEK),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_MEDIATEK_DSI),
ZIRCON_DRIVER_END(dsi_mt)
