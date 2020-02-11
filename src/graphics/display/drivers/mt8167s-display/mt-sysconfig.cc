// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mt-sysconfig.h"

#include <fbl/alloc_checker.h>

namespace mt8167s_display {

namespace {
constexpr uint32_t kColorSelOlv0 = 1;
constexpr uint32_t kRdma0SoutDsi0 = 2;
constexpr uint32_t kDsi0SelRdma0 = 1;
// By default, we will include the following modules on the same mutex:
// pwm, dither, gamma, aal, color, ccorr, rdma0, ovl0
constexpr uint32_t kDefaultMutexMod = (0xF940);
}  // namespace

zx_status_t MtSysConfig::Init(zx_device_t* parent) {
  if (initialized_) {
    return ZX_OK;
  }

  zx_status_t status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev_);
  if (status != ZX_OK) {
    return status;
  }

  // Map Sys Config MMIO
  mmio_buffer_t mmio;
  status = pdev_map_mmio_buffer(&pdev_, MMIO_DISP_SYSCFG, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map SYS CFG mmio\n");
    return status;
  }
  fbl::AllocChecker ac;
  syscfg_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
  if (!ac.check()) {
    DISP_ERROR("Could not map SYS CFG mmio\n");
    return ZX_ERR_NO_MEMORY;
  }

  // Map Mutex MMIO
  status = pdev_map_mmio_buffer(&pdev_, MMIO_DISP_MUTEX, ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio);
  if (status != ZX_OK) {
    DISP_ERROR("Could not map Mutex mmio\n");
    return status;
  }
  mutex_mmio_ = fbl::make_unique_checked<ddk::MmioBuffer>(&ac, mmio);
  if (!ac.check()) {
    DISP_ERROR("Could not map Mutex mmio\n");
    return ZX_ERR_NO_MEMORY;
  }

  // Sysconfig is ready to be used
  initialized_ = true;
  return ZX_OK;
}

zx_status_t MtSysConfig::PowerOn(SysConfigModule module) {
  ZX_DEBUG_ASSERT(initialized_);
  switch (module) {
    case MODULE_OVL0:
      MmsysCgClr0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_ovl0(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_RDMA0:
      MmsysCgClr0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_rdma0(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_COLOR0:
      MmsysCgClr0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_color0(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_CCORR:
      MmsysCgClr0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_ccorr(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_AAL:
      MmsysCgClr0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_aal(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_GAMMA:
      MmsysCgClr0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_gamma(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_DITHER:
      MmsysCgClr0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_dither(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_PWM0:
      MmsysCgClr1Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_pwm0_26m(1).WriteTo(&(*syscfg_mmio_));
      MmsysCgClr1Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_pwm0_mm(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_DSI0:
      MmsysCgClr1Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_dsi0_dig(1).WriteTo(&(*syscfg_mmio_));
      MmsysCgClr1Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_dsi0_eng(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_SMI:
      MmsysCgClr0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_smi_larb0(1).WriteTo(&(*syscfg_mmio_));
      MmsysCgClr0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_smi_common(1).WriteTo(&(*syscfg_mmio_));
      break;
    default:
      DISP_ERROR("Unknown/Unsupported module %d\n", module);
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t MtSysConfig::PowerDown(SysConfigModule module) {
  ZX_DEBUG_ASSERT(initialized_);
  switch (module) {
    case MODULE_OVL0:
      MmsysCgSet0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_ovl0(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_RDMA0:
      MmsysCgSet0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_rdma0(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_COLOR0:
      MmsysCgSet0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_color0(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_CCORR:
      MmsysCgSet0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_ccorr(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_AAL:
      MmsysCgSet0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_aal(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_GAMMA:
      MmsysCgSet0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_gamma(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_DITHER:
      MmsysCgSet0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_dither(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_PWM0:
      MmsysCgSet1Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_pwm0_26m(1).WriteTo(&(*syscfg_mmio_));
      MmsysCgSet1Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_pwm0_mm(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_DSI0:
      MmsysCgSet1Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_dsi0_dig(1).WriteTo(&(*syscfg_mmio_));
      MmsysCgSet1Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_dsi0_eng(1).WriteTo(&(*syscfg_mmio_));
      break;
    case MODULE_SMI:
      MmsysCgSet0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_smi_larb0(1).WriteTo(&(*syscfg_mmio_));
      MmsysCgSet0Reg::Get().ReadFrom(&(*syscfg_mmio_)).set_smi_common(1).WriteTo(&(*syscfg_mmio_));
      break;
    default:
      DISP_ERROR("Unknown/Unsupported module %d\n", module);
      return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

zx_status_t MtSysConfig::CreateDefaultPath() {
  ZX_DEBUG_ASSERT(initialized_);
  // 1) OVL0 to color. Need to connect OVL0 MOUT to Color0 Sel
  DispOvl0MoutEnReg::Get().ReadFrom(&(*syscfg_mmio_)).set_out_color(1).WriteTo(&(*syscfg_mmio_));

  DispColor0SelInReg::Get()
      .ReadFrom(&(*syscfg_mmio_))
      .set_sel(kColorSelOlv0)
      .WriteTo(&(*syscfg_mmio_));

  // No muxing from color to dither (includes color, ccorr, aal, gamma, dither)

  // 2) Dither to RDMA. RDMA has only 1 input which is from dither. So only dither mout needed
  DispDitherMoutEnReg::Get().ReadFrom(&(*syscfg_mmio_)).set_out_rdma0(1).WriteTo(&(*syscfg_mmio_));

  // 3) Connect RDMA to DSI. RDMA has single ouput
  DispRdma0SoutSelInReg::Get()
      .ReadFrom(&(*syscfg_mmio_))
      .set_sel(kRdma0SoutDsi0)
      .WriteTo(&(*syscfg_mmio_));
  Dsi0SelInReg::Get().ReadFrom(&(*syscfg_mmio_)).set_sel(kDsi0SelRdma0).WriteTo(&(*syscfg_mmio_));
  return ZX_OK;
}

zx_status_t MtSysConfig::ClearDefaultPath() {
  ZX_DEBUG_ASSERT(initialized_);
  DispOvl0MoutEnReg::Get().ReadFrom(&(*syscfg_mmio_)).set_out_color(0).WriteTo(&(*syscfg_mmio_));
  DispDitherMoutEnReg::Get().ReadFrom(&(*syscfg_mmio_)).set_out_rdma0(0).WriteTo(&(*syscfg_mmio_));
  DispRdma0SoutSelInReg::Get()
      .ReadFrom(&(*syscfg_mmio_))
      .set_sel(kRdma0SoutDsi0)
      .WriteTo(&(*syscfg_mmio_));
  return ZX_OK;
}

zx_status_t MtSysConfig::MutexClear() {
  ZX_DEBUG_ASSERT(initialized_);
  Mutex0ModReg::Get().FromValue(0).WriteTo(&(*mutex_mmio_));
  Mutex0SofReg::Get().FromValue(0).WriteTo(&(*mutex_mmio_));
  MutexReset();
  return ZX_OK;
}

zx_status_t MtSysConfig::MutexEnable() {
  ZX_DEBUG_ASSERT(initialized_);
  Mutex0EnReg::Get().ReadFrom(&(*mutex_mmio_)).set_enable(1).WriteTo(&(*mutex_mmio_));
  return ZX_OK;
}

zx_status_t MtSysConfig::MutexDisable() {
  ZX_DEBUG_ASSERT(initialized_);
  Mutex0EnReg::Get().ReadFrom(&(*mutex_mmio_)).set_enable(0).WriteTo(&(*mutex_mmio_));
  return ZX_OK;
}

zx_status_t MtSysConfig::MutexReset() {
  Mutex0RstReg::Get().ReadFrom(&(*mutex_mmio_)).set_reset(1).WriteTo(&(*mutex_mmio_));
  Mutex0RstReg::Get().ReadFrom(&(*mutex_mmio_)).set_reset(0).WriteTo(&(*mutex_mmio_));
  return ZX_OK;
}

zx_status_t MtSysConfig::MutexSetDefault() {
  ZX_DEBUG_ASSERT(initialized_);
  Mutex0ModReg::Get().FromValue(kDefaultMutexMod).WriteTo(&(*mutex_mmio_));
  Mutex0SofReg::Get().FromValue(1).WriteTo(&(*mutex_mmio_));
  return ZX_OK;
}

void MtSysConfig::PrintRegisters() {
  ZX_DEBUG_ASSERT(initialized_);
  zxlogf(INFO, "Dumping MtSysConfig Registers\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "SYSCONFIG_DISP_OVL0_MOUT_EN = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_DISP_OVL0_MOUT_EN));
  zxlogf(INFO, "SYSCONFIG_DISP_DITHER_MOUT_EN = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_DISP_DITHER_MOUT_EN));
  zxlogf(INFO, "SYSCONFIG_DISP_UFOE_MOUT_EN = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_DISP_UFOE_MOUT_EN));
  zxlogf(INFO, "SYSCONFIG_DISP_COLOR0_SEL_IN = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_DISP_COLOR0_SEL_IN));
  zxlogf(INFO, "SYSCONFIG_DISP_UFOE_SEL_IN = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_DISP_UFOE_SEL_IN));
  zxlogf(INFO, "SYSCONFIG_DSI0_SEL_IN = 0x%x\n", syscfg_mmio_->Read32(SYSCONFIG_DSI0_SEL_IN));
  zxlogf(INFO, "SYSCONFIG_DISP_RDMA0_SOUT_SEL_IN = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_DISP_RDMA0_SOUT_SEL_IN));
  zxlogf(INFO, "SYSCONFIG_MMSYS_MISC = 0x%x\n", syscfg_mmio_->Read32(SYSCONFIG_MMSYS_MISC));
  zxlogf(INFO, "SYSCONFIG_MMSYS_CG_CON0 = 0x%x\n", syscfg_mmio_->Read32(SYSCONFIG_MMSYS_CG_CON0));
  zxlogf(INFO, "SYSCONFIG_MMSYS_CG_SET0 = 0x%x\n", syscfg_mmio_->Read32(SYSCONFIG_MMSYS_CG_SET0));
  zxlogf(INFO, "SYSCONFIG_MMSYS_CG_CLR0 = 0x%x\n", syscfg_mmio_->Read32(SYSCONFIG_MMSYS_CG_CLR0));
  zxlogf(INFO, "SYSCONFIG_MMSYS_CG_CON1 = 0x%x\n", syscfg_mmio_->Read32(SYSCONFIG_MMSYS_CG_CON1));
  zxlogf(INFO, "SYSCONFIG_MMSYS_CG_SET1 = 0x%x\n", syscfg_mmio_->Read32(SYSCONFIG_MMSYS_CG_SET1));
  zxlogf(INFO, "SYSCONFIG_MMSYS_CG_CLR1 = 0x%x\n", syscfg_mmio_->Read32(SYSCONFIG_MMSYS_CG_CLR1));
  zxlogf(INFO, "SYSCONFIG_MMSYS_HW_DCM_DIS0 = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_MMSYS_HW_DCM_DIS0));
  zxlogf(INFO, "SYSCONFIG_MMSYS_HW_DCM_DIS_SET0 = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_MMSYS_HW_DCM_DIS_SET0));
  zxlogf(INFO, "SYSCONFIG_MMSYS_HW_DCM_DIS_CLR0 = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_MMSYS_HW_DCM_DIS_CLR0));
  zxlogf(INFO, "SYSCONFIG_MMSYS_SW0_RST_B = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_MMSYS_SW0_RST_B));
  zxlogf(INFO, "SYSCONFIG_MMSYS_SW1_RST_B = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_MMSYS_SW1_RST_B));
  zxlogf(INFO, "SYSCONFIG_MMSYS_LCM_RST_B = 0x%x\n",
         syscfg_mmio_->Read32(SYSCONFIG_MMSYS_LCM_RST_B));
  zxlogf(INFO, "SYSCONFIG_MMSYS_DUMMY = 0x%x\n", syscfg_mmio_->Read32(SYSCONFIG_MMSYS_DUMMY));
  zxlogf(INFO, "######################\n\n");

  zxlogf(INFO, "Dumping Mutex Registers\n");
  zxlogf(INFO, "######################\n\n");
  zxlogf(INFO, "MUTEX_INTEN = 0x%x\n", mutex_mmio_->Read32(MUTEX_INTEN));
  zxlogf(INFO, "MUTEX_INTSTA = 0x%x\n", mutex_mmio_->Read32(MUTEX_INTSTA));
  zxlogf(INFO, "MUTEX0_EN = 0x%x\n", mutex_mmio_->Read32(MUTEX0_EN));
  zxlogf(INFO, "MUTEX0_RST = 0x%x\n", mutex_mmio_->Read32(MUTEX0_RST));
  zxlogf(INFO, "MUTEX0_MOD = 0x%x\n", mutex_mmio_->Read32(MUTEX0_MOD));
  zxlogf(INFO, "MUTEX0_SOF = 0x%x\n", mutex_mmio_->Read32(MUTEX0_SOF));
  zxlogf(INFO, "######################\n\n");
}

}  // namespace mt8167s_display
