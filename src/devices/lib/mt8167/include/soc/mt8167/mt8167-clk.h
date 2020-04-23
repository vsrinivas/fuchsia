// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_CLK_H_
#define SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_CLK_H_

namespace board_mt8167 {

enum Mt8167Clk {
  // kClkGatingCtrl0
  kClkPwmMm,
  kClkCamMm,
  kClkMfgMm,
  kClkSpm52m,
  kClkMipi26mDbg,
  kClkScamMm,
  kClkSmiMm,

  // kClkGatingCtrl1
  kClkThem,
  kClkApdma,
  kClkI2c0,
  kClkI2c1,
  kClkAuxadc1,
  kClkNfi,
  kClkNfiecc,
  kClkDebugsys,
  kClkPwm,
  kClkUart0,
  kClkUart1,
  kClkBtif,
  kClkUsb,
  kClkFlashif26m,
  kClkAuxadc2,
  kClkI2c2,
  kClkMsdc0,
  kClkMsdc1,
  kClkNfi2x,
  kClkPmicwrapAp,
  kClkSej,
  kClkMemslpDlyer,
  kClkSpi,
  kClkApxgpt,
  kClkAudio,
  kClkPmicwrapMd,
  kClkPmicwrapConn,
  kClkPmicwrap26m,
  kClkAuxAdc,
  kClkAuxTp,

  // kClkGatingCtrl2
  kClkMsdc2,
  kClkRbist,
  kClkNfiBus,
  kClkGce,
  kClkTrng,
  kClkSej13m,
  kClkAes,
  kClkPwmB,
  kClkPwm1Fb,
  kClkPwm2Fb,
  kClkPwm3Fb,
  kClkPwm4Fb,
  kClkPwm5Fb,
  kClkUsb1p,
  kClkFlashifFreerun,
  kClk26mHdmiSifm,
  kClk26mCec,
  kClk32kCec,
  kClk66mEth,
  kClk133mEth,
  kClkFeth25m,
  kClkFeth50m,
  kClkFlashifAxi,
  kClkUsbif,
  kClkUart2,
  kClkBsi,
  kClkGcpuB,
  kClkMsdc0Infra,
  kClkMsdc1Infra,
  kClkMsdc2Infra,
  kClkUsb78m,

  // kClkGatingCtrl3
  kClkRgSpinor,
  kClkRgMsdc2,
  kClkRgEth,
  kClkRgVdec,
  kClkRgFdpi0,
  kClkRgFdpi1,
  kClkRgAxiMfg,
  kClkRgSlowMfg,
  kClkRgAud1,
  kClkRgAud2,
  kClkRgAudEngen1,
  kClkRgAudEngen2,
  kClkRgI2c,
  kClkRgPwmInfra,
  kClkRgAudSpdifIn,
  kClkRgUart2,
  kClkRgBsi,
  kClkRgDbgAtclk,
  kClkRgNfiecc,

  // kClkGatingCtrl4
  kClkRgApll1D2En,
  kClkRgApll1D4En,
  kClkRgApll1D8En,
  kClkRgApll2D2En,
  kClkRgApll2D4En,
  kClkRgApll2D8En,

};

}  // namespace board_mt8167

#endif  // SRC_DEVICES_LIB_MT8167_INCLUDE_SOC_MT8167_MT8167_CLK_H_
