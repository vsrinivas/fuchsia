// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_CLK_MSM8X53_CLK_MSM8X53_CLK_REGS_H_
#define ZIRCON_SYSTEM_DEV_CLK_MSM8X53_CLK_MSM8X53_CLK_REGS_H_

namespace msm8x53 {

// Branch Clock Register Offsets.
constexpr uint32_t kApc0VoltageDroopDetectorGpll0Cbcr = 0x78004;
constexpr uint32_t kApc1VoltageDroopDetectorGpll0Cbcr = 0x79004;
constexpr uint32_t kBimcGfxCbcr = 0x59034;
constexpr uint32_t kBimcGpuCbcr = 0x59030;
constexpr uint32_t kBlsp1Qup1I2cAppsCbcr = 0x2008;
constexpr uint32_t kBlsp1Qup1SpiAppsCbcr = 0x2004;
constexpr uint32_t kBlsp1Qup2I2cAppsCbcr = 0x3010;
constexpr uint32_t kBlsp1Qup2SpiAppsCbcr = 0x300c;
constexpr uint32_t kBlsp1Qup3I2cAppsCbcr = 0x4020;
constexpr uint32_t kBlsp1Qup3SpiAppsCbcr = 0x401c;
constexpr uint32_t kBlsp1Qup4I2cAppsCbcr = 0x5020;
constexpr uint32_t kBlsp1Qup4SpiAppsCbcr = 0x501c;
constexpr uint32_t kBlsp1Uart1AppsCbcr = 0x203c;
constexpr uint32_t kBlsp1Uart2AppsCbcr = 0x302c;
constexpr uint32_t kBlsp2Qup1I2cAppsCbcr = 0xc008;
constexpr uint32_t kBlsp2Qup1SpiAppsCbcr = 0xc004;
constexpr uint32_t kBlsp2Qup2I2cAppsCbcr = 0xd010;
constexpr uint32_t kBlsp2Qup2SpiAppsCbcr = 0xd00c;
constexpr uint32_t kBlsp2Qup3I2cAppsCbcr = 0xf020;
constexpr uint32_t kBlsp2Qup3SpiAppsCbcr = 0xf01c;
constexpr uint32_t kBlsp2Qup4I2cAppsCbcr = 0x18020;
constexpr uint32_t kBlsp2Qup4SpiAppsCbcr = 0x1801c;
constexpr uint32_t kBlsp2Uart1AppsCbcr = 0xc03c;
constexpr uint32_t kBlsp2Uart2AppsCbcr = 0xd02c;
constexpr uint32_t kCamssAhbCbcr = 0x56004;
constexpr uint32_t kCamssCciAhbCbcr = 0x5101c;
constexpr uint32_t kCamssCciCbcr = 0x51018;
constexpr uint32_t kCamssCppAhbCbcr = 0x58040;
constexpr uint32_t kCamssCppAxiCbcr = 0x58064;
constexpr uint32_t kCamssCppCbcr = 0x5803c;
constexpr uint32_t kCamssCsi0AhbCbcr = 0x4e040;
constexpr uint32_t kCamssCsi0Cbcr = 0x4e03c;
constexpr uint32_t kCamssCsi0Csiphy3pCbcr = 0x58090;
constexpr uint32_t kCamssCsi0phyCbcr = 0x4e048;
constexpr uint32_t kCamssCsi0phytimerCbcr = 0x4e01c;
constexpr uint32_t kCamssCsi0pixCbcr = 0x4e058;
constexpr uint32_t kCamssCsi0rdiCbcr = 0x4e050;
constexpr uint32_t kCamssCsi1AhbCbcr = 0x4f040;
constexpr uint32_t kCamssCsi1Cbcr = 0x4f03c;
constexpr uint32_t kCamssCsi1Csiphy3pCbcr = 0x580a0;
constexpr uint32_t kCamssCsi1phyCbcr = 0x4f048;
constexpr uint32_t kCamssCsi1phytimerCbcr = 0x4f01c;
constexpr uint32_t kCamssCsi1pixCbcr = 0x4f058;
constexpr uint32_t kCamssCsi1rdiCbcr = 0x4f050;
constexpr uint32_t kCamssCsi2AhbCbcr = 0x3c040;
constexpr uint32_t kCamssCsi2Cbcr = 0x3c03c;
constexpr uint32_t kCamssCsi2Csiphy3pCbcr = 0x580b0;
constexpr uint32_t kCamssCsi2phyCbcr = 0x3c048;
constexpr uint32_t kCamssCsi2phytimerCbcr = 0x4f068;
constexpr uint32_t kCamssCsi2pixCbcr = 0x3c058;
constexpr uint32_t kCamssCsi2rdiCbcr = 0x3c050;
constexpr uint32_t kCamssCsiVfe0Cbcr = 0x58050;
constexpr uint32_t kCamssCsiVfe1Cbcr = 0x58074;
constexpr uint32_t kCamssGp0Cbcr = 0x54018;
constexpr uint32_t kCamssGp1Cbcr = 0x55018;
constexpr uint32_t kCamssIspifAhbCbcr = 0x50004;
constexpr uint32_t kCamssJpeg0Cbcr = 0x57020;
constexpr uint32_t kCamssJpegAhbCbcr = 0x57024;
constexpr uint32_t kCamssJpegAxiCbcr = 0x57028;
constexpr uint32_t kCamssMclk0Cbcr = 0x52018;
constexpr uint32_t kCamssMclk1Cbcr = 0x53018;
constexpr uint32_t kCamssMclk2Cbcr = 0x5c018;
constexpr uint32_t kCamssMclk3Cbcr = 0x5e018;
constexpr uint32_t kCamssMicroAhbCbcr = 0x5600c;
constexpr uint32_t kCamssTopAhbCbcr = 0x5a014;
constexpr uint32_t kCamssVfe0Cbcr = 0x58038;
constexpr uint32_t kCamssVfe1AhbCbcr = 0x58060;
constexpr uint32_t kCamssVfe1AxiCbcr = 0x58068;
constexpr uint32_t kCamssVfe1Cbcr = 0x5805c;
constexpr uint32_t kCamssVfeAhbCbcr = 0x58044;
constexpr uint32_t kCamssVfeAxiCbcr = 0x58048;
constexpr uint32_t kDccCbcr = 0x77004;
constexpr uint32_t kGp1Cbcr = 0x8000;
constexpr uint32_t kGp2Cbcr = 0x9000;
constexpr uint32_t kGp3Cbcr = 0xa000;
constexpr uint32_t kMdssAhbCbcr = 0x4d07c;
constexpr uint32_t kMdssAxiCbcr = 0x4d080;
constexpr uint32_t kMdssByte0Cbcr = 0x4d094;
constexpr uint32_t kMdssByte1Cbcr = 0x4d0a0;
constexpr uint32_t kMdssEsc0Cbcr = 0x4d098;
constexpr uint32_t kMdssEsc1Cbcr = 0x4d09c;
constexpr uint32_t kMdssMdpCbcr = 0x4d088;
constexpr uint32_t kMdssPclk0Cbcr = 0x4d084;
constexpr uint32_t kMdssPclk1Cbcr = 0x4d0a4;
constexpr uint32_t kMdssVsyncCbcr = 0x4d090;
constexpr uint32_t kMssCfgAhbCbcr = 0x49000;
constexpr uint32_t kMssQ6BimcAxiCbcr = 0x49004;
constexpr uint32_t kOxiliAhbCbcr = 0x59028;
constexpr uint32_t kOxiliAonCbcr = 0x59044;
constexpr uint32_t kOxiliGfx3dCbcr = 0x59020;
constexpr uint32_t kOxiliTimerCbcr = 0x59040;
constexpr uint32_t kPcnocUsb3AxiCbcr = 0x3f038;
constexpr uint32_t kPdm2Cbcr = 0x4400c;
constexpr uint32_t kPdmAhbCbcr = 0x44004;
constexpr uint32_t kRbcprGfxCbcr = 0x3a004;
constexpr uint32_t kSdcc1AhbCbcr = 0x4201c;
constexpr uint32_t kSdcc1AppsCbcr = 0x42018;
constexpr uint32_t kSdcc1IceCoreCbcr = 0x5d014;
constexpr uint32_t kSdcc2AhbCbcr = 0x4301c;
constexpr uint32_t kSdcc2AppsCbcr = 0x43018;
constexpr uint32_t kUsb30MasterCbcr = 0x3f000;
constexpr uint32_t kUsb30MockUtmiCbcr = 0x3f00;
constexpr uint32_t kUsb30SleepCbcr = 0x3f004;
constexpr uint32_t kUsb3AuxCbcr = 0x3f044;
constexpr uint32_t kUsbPhyCfgAhbCbcr = 0x3f080;
constexpr uint32_t kVenus0AhbCbcr = 0x4c020;
constexpr uint32_t kVenus0AxiCbcr = 0x4c024;
constexpr uint32_t kVenus0Core0Vcodec0Cbcr = 0x4c02c;
constexpr uint32_t kVenus0Vcodec0Cbcr = 0x4c01c;

// Voter Clock Register Offsets.
// CBCR Regs.
constexpr uint32_t kVfe1TbuCbcr = 0x12090;
constexpr uint32_t kBootRomAhbCbcr = 0x1300c;
constexpr uint32_t kBlsp2AhbCbcr = 0xb008;
constexpr uint32_t kSmmuCfgCbcr = 0x12038;
constexpr uint32_t kMdpTbuCbcr = 0x1201c;
constexpr uint32_t kApssAhbCbcr = 0x4601c;
constexpr uint32_t kCryptoAxiCbcr = 0x16020;
constexpr uint32_t kBlsp1AhbCbcr = 0x1008;
constexpr uint32_t kQdssDapCbcr = 0x29084;
constexpr uint32_t kCppTbuCbcr = 0x12040;
constexpr uint32_t kCryptoCbcr = 0x1601c;
constexpr uint32_t kVfeTbuCbcr = 0x1203c;
constexpr uint32_t kJpegTbuCbcr = 0x12034;
constexpr uint32_t kVenusTbuCbcr = 0x12014;
constexpr uint32_t kCryptoAhbCbcr = 0x16024;
constexpr uint32_t kPrngAhbCbcr = 0x13004;
constexpr uint32_t kApssAxiCbcr = 0x46020;
constexpr uint32_t kApssTcuAsyncCbcr = 0x12018;
// Vote Regs.
constexpr uint32_t kApcsClockBranchEnaVote = 0x45004;
constexpr uint32_t kApcsSmmuClockBranchEnaVote = 0x4500C;

// RCG Command Registers
constexpr uint32_t kCamssTopAhbCmdRcgr = 0x5A000;
constexpr uint32_t kCsi0CmdRcgr = 0x4E020;
constexpr uint32_t kApssAhbCmdRcgr = 0x46000;
constexpr uint32_t kCsi1CmdRcgr = 0x4F020;
constexpr uint32_t kCsi2CmdRcgr = 0x3C020;
constexpr uint32_t kVfe0CmdRcgr = 0x58000;
constexpr uint32_t kGfx3dCmdRcgr = 0x59000;
constexpr uint32_t kVcodec0CmdRcgr = 0x4C000;
constexpr uint32_t kCppCmdRcgr = 0x58018;
constexpr uint32_t kJpeg0CmdRcgr = 0x57000;
constexpr uint32_t kMdpCmdRcgr = 0x4D014;
constexpr uint32_t kPclk0CmdRcgr = 0x4D000;
constexpr uint32_t kPclk1CmdRcgr = 0x4D0B8;
constexpr uint32_t kUsb30MasterCmdRcgr = 0x3F00C;
constexpr uint32_t kVfe1CmdRcgr = 0x58054;
constexpr uint32_t kApc0VoltageDroopDetectorCmdRcgr = 0x78008;
constexpr uint32_t kApc1VoltageDroopDetectorCmdRcgr = 0x79008;
constexpr uint32_t kBlsp1Qup1I2cAppsCmdRcgr = 0x0200C;
constexpr uint32_t kBlsp1Qup1SpiAppsCmdRcgr = 0x02024;
constexpr uint32_t kBlsp1Qup2I2cAppsCmdRcgr = 0x03000;
constexpr uint32_t kBlsp1Qup2SpiAppsCmdRcgr = 0x03014;
constexpr uint32_t kBlsp1Qup3I2cAppsCmdRcgr = 0x04000;
constexpr uint32_t kBlsp1Qup3SpiAppsCmdRcgr = 0x04024;
constexpr uint32_t kBlsp1Qup4I2cAppsCmdRcgr = 0x05000;
constexpr uint32_t kBlsp1Qup4SpiAppsCmdRcgr = 0x05024;
constexpr uint32_t kBlsp1Uart1AppsCmdRcgr = 0x02044;
constexpr uint32_t kBlsp1Uart2AppsCmdRcgr = 0x03034;
constexpr uint32_t kBlsp2Qup1I2cAppsCmdRcgr = 0x0C00C;
constexpr uint32_t kBlsp2Qup1SpiAppsCmdRcgr = 0x0C024;
constexpr uint32_t kBlsp2Qup2I2cAppsCmdRcgr = 0x0D000;
constexpr uint32_t kBlsp2Qup2SpiAppsCmdRcgr = 0x0D014;
constexpr uint32_t kBlsp2Qup3I2cAppsCmdRcgr = 0x0F000;
constexpr uint32_t kBlsp2Qup3SpiAppsCmdRcgr = 0x0F024;
constexpr uint32_t kBlsp2Qup4I2cAppsCmdRcgr = 0x18000;
constexpr uint32_t kBlsp2Qup4SpiAppsCmdRcgr = 0x18024;
constexpr uint32_t kBlsp2Uart1AppsCmdRcgr = 0x0C044;
constexpr uint32_t kBlsp2Uart2AppsCmdRcgr = 0x0D034;
constexpr uint32_t kCciCmdRcgr = 0x51000;
constexpr uint32_t kCsi0pCmdRcgr = 0x58084;
constexpr uint32_t kCsi1pCmdRcgr = 0x58094;
constexpr uint32_t kCsi2pCmdRcgr = 0x580A4;
constexpr uint32_t kCamssGp0CmdRcgr = 0x54000;
constexpr uint32_t kCamssGp1CmdRcgr = 0x55000;
constexpr uint32_t kMclk0CmdRcgr = 0x52000;
constexpr uint32_t kMclk1CmdRcgr = 0x53000;
constexpr uint32_t kMclk2CmdRcgr = 0x5C000;
constexpr uint32_t kMclk3CmdRcgr = 0x5E000;
constexpr uint32_t kCsi0phytimerCmdRcgr = 0x4E000;
constexpr uint32_t kCsi1phytimerCmdRcgr = 0x4F000;
constexpr uint32_t kCsi2phytimerCmdRcgr = 0x4F05C;
constexpr uint32_t kCryptoCmdRcgr = 0x16004;
constexpr uint32_t kGp1CmdRcgr = 0x08004;
constexpr uint32_t kGp2CmdRcgr = 0x09004;
constexpr uint32_t kGp3CmdRcgr = 0x0A004;
constexpr uint32_t kByte0CmdRcgr = 0x4D044;
constexpr uint32_t kByte1CmdRcgr = 0x4D0B0;
constexpr uint32_t kEsc0CmdRcgr = 0x4D05C;
constexpr uint32_t kEsc1CmdRcgr = 0x4D0A8;
constexpr uint32_t kVsyncCmdRcgr = 0x4D02C;
constexpr uint32_t kPdm2CmdRcgr = 0x44010;
constexpr uint32_t kRbcprGfxCmdRcgr = 0x3A00C;
constexpr uint32_t kSdcc1AppsCmdRcgr = 0x42004;
constexpr uint32_t kSdcc1IceCoreCmdRcgr = 0x5D000;
constexpr uint32_t kSdcc2AppsCmdRcgr = 0x43004;
constexpr uint32_t kUsb30MockUtmiCmdRcgr = 0x3F020;
constexpr uint32_t kUsb3AuxCmdRcgr = 0x3F05C;

// Mux Constants
constexpr uint32_t kXoSrcVal = 0;
constexpr uint32_t kXoASrcVal = 0;
constexpr uint32_t kXoPipeSrcVal = 1;
constexpr uint32_t kGpll0SrcVal = 1;
constexpr uint32_t kGpll0MainSrcVal = 2;
constexpr uint32_t kGpll0MainMockSrcVal = 3;
constexpr uint32_t kGpll0MainDiv2Usb3SrcVal = 2;
constexpr uint32_t kGpll0MainDiv2SrcVal = 4;
constexpr uint32_t kGpll0MainDiv2CciSrcVal = 3;
constexpr uint32_t kGpll0MainDiv2MmSrcVal = 5;
constexpr uint32_t kGpll0MainDiv2AxiSrcVal = 6;
constexpr uint32_t kGpll2SrcVal = 4;
constexpr uint32_t kGpll2OutMainSrcVal = 5;
constexpr uint32_t kGpll2VcodecSrcVal = 3;
constexpr uint32_t kGpll3SrcVal = 2;
constexpr uint32_t kGpll4SrcVal = 2;
constexpr uint32_t kGpll4AuxSrcVal = 2;
constexpr uint32_t kGpll4OutAuxSrcVal = 4;
constexpr uint32_t kGpll6MainSrcVal = 1;
constexpr uint32_t kGpll6SrcVal = 2;
constexpr uint32_t kGpll6MainGfxSrcVal = 3;
constexpr uint32_t kGpll6MainDiv2MockSrcVal = 2;
constexpr uint32_t kGpll6MainDiv2SrcVal = 5;
constexpr uint32_t kGpll6MainDiv2GfxSrcVal = 6;
constexpr uint32_t kGpll6AuxSrcVal = 2;
constexpr uint32_t kGpll6OutAuxSrcVal = 3;
constexpr uint32_t kUsb3PipeSrcVal = 0;
constexpr uint32_t kDsi0PhypllMmSrcVal = 1;
constexpr uint32_t kDsi1PhypllMmSrcVal = 3;
constexpr uint32_t kDsi0PhypllClkMmSrcVal = 3;
constexpr uint32_t kDsi1PhypllClkMmSrcVal = 1;

}  // namespace msm8x53

namespace clk {

typedef struct msm_clk_gate {
  uint32_t reg;
  uint32_t bit;
  uint32_t delay_us;
} msm_clk_gate_t;

struct msm_clk_branch {
  uint32_t reg;
};

struct msm_clk_voter {
  uint32_t cbcr_reg;
  uint32_t vote_reg;
  uint32_t bit;
};

enum class RcgDividerType { HalfInteger, Mnd };

class RcgFrequencyTable {
  static constexpr uint32_t kPredivMask = 0x1f;
  static constexpr uint32_t kSrcMask = 0x7;
  static constexpr uint32_t kSrcShift = 8;

 public:
  constexpr RcgFrequencyTable(uint64_t rate, uint32_t m, uint32_t n, uint32_t d2, uint32_t parent)
      : rate_(rate),
        m_(m),
        n_(n == 0 ? 0 : ~(n - m)),
        d_(~n),
        predev_parent_(((d2 - 1) & kPredivMask) | ((parent & kSrcMask) << kSrcShift)) {}

  uint64_t rate() const { return rate_; }
  uint32_t m() const { return m_; }
  uint32_t n() const { return n_; }
  uint32_t d() const { return d_; }
  uint32_t predev_parent() const { return predev_parent_; }

 private:
  const uint64_t rate_;
  const uint32_t m_;
  const uint32_t n_;
  const uint32_t d_;
  const uint32_t predev_parent_;
};

class MsmClkRcg {
 public:
  constexpr MsmClkRcg(uint32_t reg, RcgDividerType type, const RcgFrequencyTable* table,
                      size_t frequency_table_count, bool unsupported = false)
      : cmd_rcgr_reg_(reg),
        type_(type),
        frequency_table_(table),
        frequency_table_count_(frequency_table_count),
        unsupported_(unsupported) {}

  static constexpr uint32_t kCmdOffset = 0x0;
  uint32_t CmdReg() const { return cmd_rcgr_reg_ + kCmdOffset; }

  static constexpr uint32_t kCfgOffset = 0x4;
  uint32_t CfgReg() const { return cmd_rcgr_reg_ + kCfgOffset; }

  static constexpr uint32_t kMOffset = 0x8;
  uint32_t MReg() const { return cmd_rcgr_reg_ + kMOffset; }

  static constexpr uint32_t kNOffset = 0xC;
  uint32_t NReg() const { return cmd_rcgr_reg_ + kNOffset; }

  static constexpr uint32_t KDOffset = 0x10;
  uint32_t DReg() const { return cmd_rcgr_reg_ + KDOffset; }

  RcgDividerType Type() const { return type_; }
  const RcgFrequencyTable* Table() const { return frequency_table_; }
  size_t TableCount() const { return frequency_table_count_; }
  bool Unsupported() const { return unsupported_; }

 private:
  const uint32_t cmd_rcgr_reg_;
  const RcgDividerType type_;
  const RcgFrequencyTable* frequency_table_;
  const size_t frequency_table_count_;
  const bool unsupported_;
};

namespace {

class RcgClkCmd : public hwreg::RegisterBase<RcgClkCmd, uint32_t> {
 public:
  DEF_BIT(0, cfg_update);
  DEF_BIT(1, root_enable);
  DEF_BIT(31, root_status);

  static auto Read(uint32_t offset) { return hwreg::RegisterAddr<RcgClkCmd>(offset); }
};

constexpr msm_clk_gate_t kMsmClkGates[] = {
    [msm8x53::MsmClkIndex(msm8x53::kQUsbRefClk)] = {.reg = 0x41030, .bit = 0, .delay_us = 0},
    [msm8x53::MsmClkIndex(msm8x53::kUsbSSRefClk)] = {.reg = 0x5e07c, .bit = 0, .delay_us = 0},
    [msm8x53::MsmClkIndex(msm8x53::kUsb3PipeClk)] = {.reg = 0x5e040, .bit = 0, .delay_us = 50},
};

constexpr uint32_t kBranchEnable = (0x1u << 0);
constexpr struct msm_clk_branch kMsmClkBranches[] = {
    [msm8x53::MsmClkIndex(
        msm8x53::kApc0DroopDetectorGpll0Clk)] = {.reg =
                                                     msm8x53::kApc0VoltageDroopDetectorGpll0Cbcr},
    [msm8x53::MsmClkIndex(
        msm8x53::kApc1DroopDetectorGpll0Clk)] = {.reg =
                                                     msm8x53::kApc1VoltageDroopDetectorGpll0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup1I2cAppsClk)] = {.reg = msm8x53::kBlsp1Qup1I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup1SpiAppsClk)] = {.reg = msm8x53::kBlsp1Qup1SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup2I2cAppsClk)] = {.reg = msm8x53::kBlsp1Qup2I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup2SpiAppsClk)] = {.reg = msm8x53::kBlsp1Qup2SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup3I2cAppsClk)] = {.reg = msm8x53::kBlsp1Qup3I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup3SpiAppsClk)] = {.reg = msm8x53::kBlsp1Qup3SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup4I2cAppsClk)] = {.reg = msm8x53::kBlsp1Qup4I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup4SpiAppsClk)] = {.reg = msm8x53::kBlsp1Qup4SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Uart1AppsClk)] = {.reg = msm8x53::kBlsp1Uart1AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Uart2AppsClk)] = {.reg = msm8x53::kBlsp1Uart2AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup1I2cAppsClk)] = {.reg = msm8x53::kBlsp2Qup1I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup1SpiAppsClk)] = {.reg = msm8x53::kBlsp2Qup1SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup2I2cAppsClk)] = {.reg = msm8x53::kBlsp2Qup2I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup2SpiAppsClk)] = {.reg = msm8x53::kBlsp2Qup2SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup3I2cAppsClk)] = {.reg = msm8x53::kBlsp2Qup3I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup3SpiAppsClk)] = {.reg = msm8x53::kBlsp2Qup3SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup4I2cAppsClk)] = {.reg = msm8x53::kBlsp2Qup4I2cAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup4SpiAppsClk)] = {.reg = msm8x53::kBlsp2Qup4SpiAppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Uart1AppsClk)] = {.reg = msm8x53::kBlsp2Uart1AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Uart2AppsClk)] = {.reg = msm8x53::kBlsp2Uart2AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBimcGpuClk)] = {.reg = msm8x53::kBimcGpuCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCciAhbClk)] = {.reg = msm8x53::kCamssCciAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCciClk)] = {.reg = msm8x53::kCamssCciCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCppAhbClk)] = {.reg = msm8x53::kCamssCppAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCppAxiClk)] = {.reg = msm8x53::kCamssCppAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCppClk)] = {.reg = msm8x53::kCamssCppCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0AhbClk)] = {.reg = msm8x53::kCamssCsi0AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0Clk)] = {.reg = msm8x53::kCamssCsi0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0Csiphy3pClk)] = {.reg =
                                                                  msm8x53::kCamssCsi0Csiphy3pCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0phyClk)] = {.reg = msm8x53::kCamssCsi0phyCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0pixClk)] = {.reg = msm8x53::kCamssCsi0pixCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0rdiClk)] = {.reg = msm8x53::kCamssCsi0rdiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1AhbClk)] = {.reg = msm8x53::kCamssCsi1AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1Clk)] = {.reg = msm8x53::kCamssCsi1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1Csiphy3pClk)] = {.reg =
                                                                  msm8x53::kCamssCsi1Csiphy3pCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1phyClk)] = {.reg = msm8x53::kCamssCsi1phyCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1pixClk)] = {.reg = msm8x53::kCamssCsi1pixCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1rdiClk)] = {.reg = msm8x53::kCamssCsi1rdiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2AhbClk)] = {.reg = msm8x53::kCamssCsi2AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2Clk)] = {.reg = msm8x53::kCamssCsi2Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2Csiphy3pClk)] = {.reg =
                                                                  msm8x53::kCamssCsi2Csiphy3pCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2phyClk)] = {.reg = msm8x53::kCamssCsi2phyCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2pixClk)] = {.reg = msm8x53::kCamssCsi2pixCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2rdiClk)] = {.reg = msm8x53::kCamssCsi2rdiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsiVfe0Clk)] = {.reg = msm8x53::kCamssCsiVfe0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsiVfe1Clk)] = {.reg = msm8x53::kCamssCsiVfe1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssGp0Clk)] = {.reg = msm8x53::kCamssGp0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssGp1Clk)] = {.reg = msm8x53::kCamssGp1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssIspifAhbClk)] = {.reg = msm8x53::kCamssIspifAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssJpeg0Clk)] = {.reg = msm8x53::kCamssJpeg0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssJpegAhbClk)] = {.reg = msm8x53::kCamssJpegAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssJpegAxiClk)] = {.reg = msm8x53::kCamssJpegAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk0Clk)] = {.reg = msm8x53::kCamssMclk0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk1Clk)] = {.reg = msm8x53::kCamssMclk1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk2Clk)] = {.reg = msm8x53::kCamssMclk2Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMclk3Clk)] = {.reg = msm8x53::kCamssMclk3Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssMicroAhbClk)] = {.reg = msm8x53::kCamssMicroAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi0phytimerClk)] = {.reg =
                                                                  msm8x53::kCamssCsi0phytimerCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi1phytimerClk)] = {.reg =
                                                                  msm8x53::kCamssCsi1phytimerCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssCsi2phytimerClk)] = {.reg =
                                                                  msm8x53::kCamssCsi2phytimerCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssAhbClk)] = {.reg = msm8x53::kCamssAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssTopAhbClk)] = {.reg = msm8x53::kCamssTopAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe0Clk)] = {.reg = msm8x53::kCamssVfe0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfeAhbClk)] = {.reg = msm8x53::kCamssVfeAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfeAxiClk)] = {.reg = msm8x53::kCamssVfeAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe1AhbClk)] = {.reg = msm8x53::kCamssVfe1AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe1AxiClk)] = {.reg = msm8x53::kCamssVfe1AxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kCamssVfe1Clk)] = {.reg = msm8x53::kCamssVfe1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kDccClk)] = {.reg = msm8x53::kDccCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kGp1Clk)] = {.reg = msm8x53::kGp1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kGp2Clk)] = {.reg = msm8x53::kGp2Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kGp3Clk)] = {.reg = msm8x53::kGp3Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssAhbClk)] = {.reg = msm8x53::kMdssAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssAxiClk)] = {.reg = msm8x53::kMdssAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssByte0Clk)] = {.reg = msm8x53::kMdssByte0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssByte1Clk)] = {.reg = msm8x53::kMdssByte1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssEsc0Clk)] = {.reg = msm8x53::kMdssEsc0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssEsc1Clk)] = {.reg = msm8x53::kMdssEsc1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssMdpClk)] = {.reg = msm8x53::kMdssMdpCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssPclk0Clk)] = {.reg = msm8x53::kMdssPclk0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssPclk1Clk)] = {.reg = msm8x53::kMdssPclk1Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMdssVsyncClk)] = {.reg = msm8x53::kMdssVsyncCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMssCfgAhbClk)] = {.reg = msm8x53::kMssCfgAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kMssQ6BimcAxiClk)] = {.reg = msm8x53::kMssQ6BimcAxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kBimcGfxClk)] = {.reg = msm8x53::kBimcGfxCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kOxiliAhbClk)] = {.reg = msm8x53::kOxiliAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kOxiliAonClk)] = {.reg = msm8x53::kOxiliAonCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kOxiliGfx3dClk)] = {.reg = msm8x53::kOxiliGfx3dCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kOxiliTimerClk)] = {.reg = msm8x53::kOxiliTimerCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kPcnocUsb3AxiClk)] = {.reg = msm8x53::kPcnocUsb3AxiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kPdm2Clk)] = {.reg = msm8x53::kPdm2Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kPdmAhbClk)] = {.reg = msm8x53::kPdmAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kRbcprGfxClk)] = {.reg = msm8x53::kRbcprGfxCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1AhbClk)] = {.reg = msm8x53::kSdcc1AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1AppsClk)] = {.reg = msm8x53::kSdcc1AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1IceCoreClk)] = {.reg = msm8x53::kSdcc1IceCoreCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc2AhbClk)] = {.reg = msm8x53::kSdcc2AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kSdcc2AppsClk)] = {.reg = msm8x53::kSdcc2AppsCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsb30MasterClk)] = {.reg = msm8x53::kUsb30MasterCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsb30MockUtmiClk)] = {.reg = msm8x53::kUsb30MockUtmiCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsb30SleepClk)] = {.reg = msm8x53::kUsb30SleepCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsb3AuxClk)] = {.reg = msm8x53::kUsb3AuxCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kUsbPhyCfgAhbClk)] = {.reg = msm8x53::kUsbPhyCfgAhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kVenus0AhbClk)] = {.reg = msm8x53::kVenus0AhbCbcr},
    [msm8x53::MsmClkIndex(msm8x53::kVenus0AxiClk)] = {.reg = msm8x53::kVenus0AxiCbcr},
    [msm8x53::MsmClkIndex(
        msm8x53::kVenus0Core0Vcodec0Clk)] = {.reg = msm8x53::kVenus0Core0Vcodec0Cbcr},
    [msm8x53::MsmClkIndex(msm8x53::kVenus0Vcodec0Clk)] = {.reg = msm8x53::kVenus0Vcodec0Cbcr},
};

constexpr RcgFrequencyTable kFtblCamssTopAhbClkSrc[] = {
    RcgFrequencyTable(40000000, 0, 0, 20, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(80000000, 0, 0, 20, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblCsi0ClkSrc[] = {
    RcgFrequencyTable(100000000, 0, 0, 8, msm8x53::kGpll0MainDiv2MmSrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2SrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(465000000, 0, 0, 4, msm8x53::kGpll2SrcVal),
};

constexpr RcgFrequencyTable kFtblApssAhbClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoASrcVal),
    RcgFrequencyTable(25000000, 0, 0, 32, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(50000000, 0, 0, 32, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(100000000, 0, 0, 16, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(133330000, 0, 0, 12, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblCsi1ClkSrc[] = {
    RcgFrequencyTable(100000000, 0, 0, 8, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2OutMainSrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(465000000, 0, 0, 4, msm8x53::kGpll2OutMainSrcVal),
};

constexpr RcgFrequencyTable kFtblCsi2ClkSrc[] = {
    RcgFrequencyTable(100000000, 0, 0, 8, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2OutMainSrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(465000000, 0, 0, 4, msm8x53::kGpll2OutMainSrcVal),
};

constexpr RcgFrequencyTable kFtblVfe0ClkSrc[] = {
    RcgFrequencyTable(50000000, 0, 0, 16, msm8x53::kGpll0MainDiv2MmSrcVal),
    RcgFrequencyTable(100000000, 0, 0, 8, msm8x53::kGpll0MainDiv2MmSrcVal),
    RcgFrequencyTable(133330000, 0, 0, 12, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(160000000, 0, 0, 10, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2SrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(465000000, 0, 0, 4, msm8x53::kGpll2SrcVal),
};

constexpr RcgFrequencyTable kFtblGfx3dClkSrc[] = {};

__UNUSED constexpr RcgFrequencyTable kFtblGfx3dClkSrcSdm450[] = {};

__UNUSED constexpr RcgFrequencyTable kFtblGfx3dClkSrcSdm632[] = {};

constexpr RcgFrequencyTable kFtblVcodec0ClkSrc[] = {
    RcgFrequencyTable(114290000, 0, 0, 7, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(228570000, 0, 0, 7, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2VcodecSrcVal),
    RcgFrequencyTable(360000000, 0, 0, 6, msm8x53::kGpll6SrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(465000000, 0, 0, 4, msm8x53::kGpll2VcodecSrcVal),
};

__UNUSED constexpr RcgFrequencyTable kFtblVcodec0ClkSrc540MHz[] = {
    RcgFrequencyTable(114290000, 0, 0, 7, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(228570000, 0, 0, 7, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2VcodecSrcVal),
    RcgFrequencyTable(360000000, 0, 0, 6, msm8x53::kGpll6SrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(465000000, 0, 0, 4, msm8x53::kGpll2VcodecSrcVal),
    RcgFrequencyTable(540000000, 0, 0, 4, msm8x53::kGpll6SrcVal),
};

constexpr RcgFrequencyTable kFtblCppClkSrc[] = {
    RcgFrequencyTable(100000000, 0, 0, 8, msm8x53::kGpll0MainDiv2MmSrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(320000000, 0, 0, 5, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(465000000, 0, 0, 4, msm8x53::kGpll2SrcVal),
};

constexpr RcgFrequencyTable kFtblJpeg0ClkSrc[] = {
    RcgFrequencyTable(66670000, 0, 0, 12, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(133330000, 0, 0, 12, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2OutMainSrcVal),
    RcgFrequencyTable(320000000, 0, 0, 5, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblMdpClkSrc[] = {
    RcgFrequencyTable(50000000, 0, 0, 16, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(80000000, 0, 0, 10, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(160000000, 0, 0, 5, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(320000000, 0, 0, 5, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblPclk0ClkSrc[] = {};

constexpr RcgFrequencyTable kFtblPclk1ClkSrc[] = {};

constexpr RcgFrequencyTable kFtblUsb30MasterClkSrc[] = {
    RcgFrequencyTable(80000000, 0, 0, 10, msm8x53::kGpll0MainDiv2Usb3SrcVal),
    RcgFrequencyTable(100000000, 0, 0, 16, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(133330000, 0, 0, 12, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblVfe1ClkSrc[] = {
    RcgFrequencyTable(50000000, 0, 0, 16, msm8x53::kGpll0MainDiv2MmSrcVal),
    RcgFrequencyTable(100000000, 0, 0, 8, msm8x53::kGpll0MainDiv2MmSrcVal),
    RcgFrequencyTable(133330000, 0, 0, 12, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(160000000, 0, 0, 10, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2SrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(465000000, 0, 0, 4, msm8x53::kGpll2SrcVal),
};

constexpr RcgFrequencyTable kFtblApc0DroopDetectorClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(576000000, 0, 0, 4, msm8x53::kGpll4SrcVal),
};

constexpr RcgFrequencyTable kFtblApc1DroopDetectorClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
    RcgFrequencyTable(400000000, 0, 0, 4, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(576000000, 0, 0, 4, msm8x53::kGpll4SrcVal),
};

constexpr RcgFrequencyTable kFtblBlspI2cAppsClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
    RcgFrequencyTable(25000000, 0, 0, 32, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(50000000, 0, 0, 32, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblBlspSpiAppsClkSrc[] = {
    RcgFrequencyTable(960000, 1, 2, 20, msm8x53::kXoSrcVal),
    RcgFrequencyTable(4800000, 0, 0, 8, msm8x53::kXoSrcVal),
    RcgFrequencyTable(9600000, 0, 0, 4, msm8x53::kXoSrcVal),
    RcgFrequencyTable(12500000, 1, 2, 32, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(16000000, 1, 5, 20, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
    RcgFrequencyTable(25000000, 1, 2, 32, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(50000000, 0, 0, 32, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblBlspUartAppsClkSrc[] = {
    RcgFrequencyTable(3686400, 144, 15625, 2, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(7372800, 288, 15625, 2, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(14745600, 576, 15625, 2, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(16000000, 1, 5, 10, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
    RcgFrequencyTable(24000000, 3, 100, 2, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(25000000, 1, 2, 32, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(32000000, 1, 25, 2, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(40000000, 1, 20, 2, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(46400000, 29, 500, 2, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(48000000, 3, 50, 2, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(51200000, 8, 125, 2, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(56000000, 7, 100, 2, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(58982400, 1152, 15625, 2, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(60000000, 3, 40, 2, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(64000000, 2, 25, 2, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblCciClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
    RcgFrequencyTable(37500000, 3, 32, 2, msm8x53::kGpll0MainDiv2CciSrcVal),
};

constexpr RcgFrequencyTable kFtblCsi0pClkSrc[] = {
    RcgFrequencyTable(66670000, 0, 0, 12, msm8x53::kGpll0MainDiv2MmSrcVal),
    RcgFrequencyTable(133330000, 0, 0, 12, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2SrcVal),
};

constexpr RcgFrequencyTable kFtblCsi1pClkSrc[] = {
    RcgFrequencyTable(66670000, 0, 0, 12, msm8x53::kGpll0MainDiv2MmSrcVal),
    RcgFrequencyTable(133330000, 0, 0, 12, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2SrcVal),
};

constexpr RcgFrequencyTable kFtblCsi2pClkSrc[] = {
    RcgFrequencyTable(66670000, 0, 0, 12, msm8x53::kGpll0MainDiv2MmSrcVal),
    RcgFrequencyTable(133330000, 0, 0, 12, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(310000000, 0, 0, 6, msm8x53::kGpll2SrcVal),
};

constexpr RcgFrequencyTable kFtblCamssGp0ClkSrc[] = {
    RcgFrequencyTable(50000000, 0, 0, 16, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(100000000, 0, 0, 16, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblCamssGp1ClkSrc[] = {
    RcgFrequencyTable(50000000, 0, 0, 16, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(100000000, 0, 0, 16, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblMclk0ClkSrc[] = {
    RcgFrequencyTable(24000000, 2, 45, 2, msm8x53::kGpll6MainDiv2SrcVal),
    RcgFrequencyTable(33330000, 0, 0, 24, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(36610000, 2, 59, 2, msm8x53::kGpll6SrcVal),
    RcgFrequencyTable(66667000, 0, 0, 24, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblMclk1ClkSrc[] = {
    RcgFrequencyTable(24000000, 2, 45, 2, msm8x53::kGpll6MainDiv2SrcVal),
    RcgFrequencyTable(33330000, 0, 0, 24, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(36610000, 2, 59, 2, msm8x53::kGpll6SrcVal),
    RcgFrequencyTable(66667000, 0, 0, 24, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblMclk2ClkSrc[] = {
    RcgFrequencyTable(24000000, 2, 45, 2, msm8x53::kGpll6MainDiv2SrcVal),
    RcgFrequencyTable(33330000, 0, 0, 24, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(36610000, 2, 59, 2, msm8x53::kGpll6SrcVal),
    RcgFrequencyTable(66667000, 0, 0, 24, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblMclk3ClkSrc[] = {
    RcgFrequencyTable(24000000, 2, 45, 2, msm8x53::kGpll6MainDiv2SrcVal),
    RcgFrequencyTable(33330000, 0, 0, 24, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(36610000, 2, 59, 2, msm8x53::kGpll6SrcVal),
    RcgFrequencyTable(66667000, 0, 0, 24, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblCsi0phytimerClkSrc[] = {
    RcgFrequencyTable(100000000, 0, 0, 8, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblCsi1phytimerClkSrc[] = {
    RcgFrequencyTable(100000000, 0, 0, 8, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblCsi2phytimerClkSrc[] = {
    RcgFrequencyTable(100000000, 0, 0, 8, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(266670000, 0, 0, 6, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblCryptoClkSrc[] = {
    RcgFrequencyTable(40000000, 0, 0, 20, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(80000000, 0, 0, 20, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(100000000, 0, 0, 16, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(160000000, 0, 0, 10, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblGp1ClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
};

constexpr RcgFrequencyTable kFtblGp2ClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
};

constexpr RcgFrequencyTable kFtblGp3ClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
};

constexpr RcgFrequencyTable kFtblByte0ClkSrc[] = {};

constexpr RcgFrequencyTable kFtblByte1ClkSrc[] = {};

constexpr RcgFrequencyTable kFtblEsc0ClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
};

constexpr RcgFrequencyTable kFtblEsc1ClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
};

constexpr RcgFrequencyTable kFtblVsyncClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
};

constexpr RcgFrequencyTable kFtblPdm2ClkSrc[] = {
    RcgFrequencyTable(32000000, 0, 0, 25, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(64000000, 0, 0, 25, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblRbcprGfxClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
    RcgFrequencyTable(50000000, 0, 0, 32, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblSdcc1AppsClkSrc[] = {
    RcgFrequencyTable(144000, 3, 25, 32, msm8x53::kXoSrcVal),
    RcgFrequencyTable(400000, 1, 4, 24, msm8x53::kXoSrcVal),
    RcgFrequencyTable(20000000, 1, 4, 10, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(25000000, 0, 0, 32, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(50000000, 0, 0, 32, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(100000000, 0, 0, 16, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(177770000, 0, 0, 9, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(192000000, 0, 0, 12, msm8x53::kGpll4SrcVal),
    RcgFrequencyTable(384000000, 0, 0, 6, msm8x53::kGpll4SrcVal),
};

constexpr RcgFrequencyTable kFtblSdcc1IceCoreClkSrc[] = {
    RcgFrequencyTable(80000000, 0, 0, 10, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(160000000, 0, 0, 10, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(270000000, 0, 0, 8, msm8x53::kGpll6SrcVal),
};

constexpr RcgFrequencyTable kFtblSdcc2AppsClkSrc[] = {
    RcgFrequencyTable(144000, 3, 25, 32, msm8x53::kXoSrcVal),
    RcgFrequencyTable(400000, 1, 4, 24, msm8x53::kXoSrcVal),
    RcgFrequencyTable(20000000, 1, 4, 10, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(25000000, 0, 0, 32, msm8x53::kGpll0MainDiv2SrcVal),
    RcgFrequencyTable(50000000, 0, 0, 32, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(100000000, 0, 0, 16, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(177770000, 0, 0, 9, msm8x53::kGpll0SrcVal),
    RcgFrequencyTable(192000000, 0, 0, 12, msm8x53::kGpll4AuxSrcVal),
    RcgFrequencyTable(200000000, 0, 0, 8, msm8x53::kGpll0SrcVal),
};

constexpr RcgFrequencyTable kFtblUsb30MockUtmiClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
    RcgFrequencyTable(60000000, 1, 1, 18, msm8x53::kGpll6MainDiv2MockSrcVal),
};

constexpr RcgFrequencyTable kFtblUsb3AuxClkSrc[] = {
    RcgFrequencyTable(19200000, 0, 0, 2, msm8x53::kXoSrcVal),
};

constexpr MsmClkRcg kMsmClkRcgs[] = {
    [msm8x53::MsmClkIndex(msm8x53::kCamssTopAhbClkSrc)] =
        MsmClkRcg(msm8x53::kCamssTopAhbCmdRcgr, RcgDividerType::Mnd, kFtblCamssTopAhbClkSrc,
                  countof(kFtblCamssTopAhbClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCsi0ClkSrc)] =
        MsmClkRcg(msm8x53::kCsi0CmdRcgr, RcgDividerType::HalfInteger, kFtblCsi0ClkSrc,
                  countof(kFtblCsi0ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kApssAhbClkSrc)] =
        MsmClkRcg(msm8x53::kApssAhbCmdRcgr, RcgDividerType::HalfInteger, kFtblApssAhbClkSrc,
                  countof(kFtblApssAhbClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCsi1ClkSrc)] =
        MsmClkRcg(msm8x53::kCsi1CmdRcgr, RcgDividerType::HalfInteger, kFtblCsi1ClkSrc,
                  countof(kFtblCsi1ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCsi2ClkSrc)] =
        MsmClkRcg(msm8x53::kCsi2CmdRcgr, RcgDividerType::HalfInteger, kFtblCsi2ClkSrc,
                  countof(kFtblCsi2ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kVfe0ClkSrc)] =
        MsmClkRcg(msm8x53::kVfe0CmdRcgr, RcgDividerType::HalfInteger, kFtblVfe0ClkSrc,
                  countof(kFtblVfe0ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kGfx3dClkSrc)] =
        MsmClkRcg(msm8x53::kGfx3dCmdRcgr, RcgDividerType::HalfInteger, kFtblGfx3dClkSrc,
                  countof(kFtblGfx3dClkSrc), true),
    [msm8x53::MsmClkIndex(msm8x53::kVcodec0ClkSrc)] =
        MsmClkRcg(msm8x53::kVcodec0CmdRcgr, RcgDividerType::Mnd, kFtblVcodec0ClkSrc,
                  countof(kFtblVcodec0ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCppClkSrc)] = MsmClkRcg(
        msm8x53::kCppCmdRcgr, RcgDividerType::HalfInteger, kFtblCppClkSrc, countof(kFtblCppClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kJpeg0ClkSrc)] =
        MsmClkRcg(msm8x53::kJpeg0CmdRcgr, RcgDividerType::HalfInteger, kFtblJpeg0ClkSrc,
                  countof(kFtblJpeg0ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kMdpClkSrc)] = MsmClkRcg(
        msm8x53::kMdpCmdRcgr, RcgDividerType::HalfInteger, kFtblMdpClkSrc, countof(kFtblMdpClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kPclk0ClkSrc)] = MsmClkRcg(
        msm8x53::kPclk0CmdRcgr, RcgDividerType::Mnd, kFtblPclk0ClkSrc, countof(kFtblPclk0ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kPclk1ClkSrc)] = MsmClkRcg(
        msm8x53::kPclk1CmdRcgr, RcgDividerType::Mnd, kFtblPclk1ClkSrc, countof(kFtblPclk1ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kUsb30MasterClkSrc)] =
        MsmClkRcg(msm8x53::kUsb30MasterCmdRcgr, RcgDividerType::Mnd, kFtblUsb30MasterClkSrc,
                  countof(kFtblUsb30MasterClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kVfe1ClkSrc)] =
        MsmClkRcg(msm8x53::kVfe1CmdRcgr, RcgDividerType::HalfInteger, kFtblVfe1ClkSrc,
                  countof(kFtblVfe1ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kApc0DroopDetectorClkSrc)] =
        MsmClkRcg(msm8x53::kApc0VoltageDroopDetectorCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblApc0DroopDetectorClkSrc, countof(kFtblApc0DroopDetectorClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kApc1DroopDetectorClkSrc)] =
        MsmClkRcg(msm8x53::kApc1VoltageDroopDetectorCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblApc1DroopDetectorClkSrc, countof(kFtblApc1DroopDetectorClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup1I2cAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Qup1I2cAppsCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblBlspI2cAppsClkSrc, countof(kFtblBlspI2cAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup1SpiAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Qup1SpiAppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspSpiAppsClkSrc,
                  countof(kFtblBlspSpiAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup2I2cAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Qup2I2cAppsCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblBlspI2cAppsClkSrc, countof(kFtblBlspI2cAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup2SpiAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Qup2SpiAppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspSpiAppsClkSrc,
                  countof(kFtblBlspSpiAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup3I2cAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Qup3I2cAppsCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblBlspI2cAppsClkSrc, countof(kFtblBlspI2cAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup3SpiAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Qup3SpiAppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspSpiAppsClkSrc,
                  countof(kFtblBlspSpiAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup4I2cAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Qup4I2cAppsCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblBlspI2cAppsClkSrc, countof(kFtblBlspI2cAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Qup4SpiAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Qup4SpiAppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspSpiAppsClkSrc,
                  countof(kFtblBlspSpiAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Uart1AppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Uart1AppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspUartAppsClkSrc,
                  countof(kFtblBlspUartAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1Uart2AppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp1Uart2AppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspUartAppsClkSrc,
                  countof(kFtblBlspUartAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup1I2cAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Qup1I2cAppsCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblBlspI2cAppsClkSrc, countof(kFtblBlspI2cAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup1SpiAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Qup1SpiAppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspSpiAppsClkSrc,
                  countof(kFtblBlspSpiAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup2I2cAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Qup2I2cAppsCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblBlspI2cAppsClkSrc, countof(kFtblBlspI2cAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup2SpiAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Qup2SpiAppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspSpiAppsClkSrc,
                  countof(kFtblBlspSpiAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup3I2cAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Qup3I2cAppsCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblBlspI2cAppsClkSrc, countof(kFtblBlspI2cAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup3SpiAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Qup3SpiAppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspSpiAppsClkSrc,
                  countof(kFtblBlspSpiAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup4I2cAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Qup4I2cAppsCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblBlspI2cAppsClkSrc, countof(kFtblBlspI2cAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Qup4SpiAppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Qup4SpiAppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspSpiAppsClkSrc,
                  countof(kFtblBlspSpiAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Uart1AppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Uart1AppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspUartAppsClkSrc,
                  countof(kFtblBlspUartAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2Uart2AppsClkSrc)] =
        MsmClkRcg(msm8x53::kBlsp2Uart2AppsCmdRcgr, RcgDividerType::Mnd, kFtblBlspUartAppsClkSrc,
                  countof(kFtblBlspUartAppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCciClkSrc)] = MsmClkRcg(
        msm8x53::kCciCmdRcgr, RcgDividerType::Mnd, kFtblCciClkSrc, countof(kFtblCciClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCsi0pClkSrc)] =
        MsmClkRcg(msm8x53::kCsi0pCmdRcgr, RcgDividerType::HalfInteger, kFtblCsi0pClkSrc,
                  countof(kFtblCsi0pClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCsi1pClkSrc)] =
        MsmClkRcg(msm8x53::kCsi1pCmdRcgr, RcgDividerType::HalfInteger, kFtblCsi1pClkSrc,
                  countof(kFtblCsi1pClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCsi2pClkSrc)] =
        MsmClkRcg(msm8x53::kCsi2pCmdRcgr, RcgDividerType::HalfInteger, kFtblCsi2pClkSrc,
                  countof(kFtblCsi2pClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCamssGp0ClkSrc)] =
        MsmClkRcg(msm8x53::kCamssGp0CmdRcgr, RcgDividerType::Mnd, kFtblCamssGp0ClkSrc,
                  countof(kFtblCamssGp0ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCamssGp1ClkSrc)] =
        MsmClkRcg(msm8x53::kCamssGp1CmdRcgr, RcgDividerType::Mnd, kFtblCamssGp1ClkSrc,
                  countof(kFtblCamssGp1ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kMclk0ClkSrc)] = MsmClkRcg(
        msm8x53::kMclk0CmdRcgr, RcgDividerType::Mnd, kFtblMclk0ClkSrc, countof(kFtblMclk0ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kMclk1ClkSrc)] = MsmClkRcg(
        msm8x53::kMclk1CmdRcgr, RcgDividerType::Mnd, kFtblMclk1ClkSrc, countof(kFtblMclk1ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kMclk2ClkSrc)] = MsmClkRcg(
        msm8x53::kMclk2CmdRcgr, RcgDividerType::Mnd, kFtblMclk2ClkSrc, countof(kFtblMclk2ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kMclk3ClkSrc)] = MsmClkRcg(
        msm8x53::kMclk3CmdRcgr, RcgDividerType::Mnd, kFtblMclk3ClkSrc, countof(kFtblMclk3ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCsi0phytimerClkSrc)] =
        MsmClkRcg(msm8x53::kCsi0phytimerCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblCsi0phytimerClkSrc, countof(kFtblCsi0phytimerClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCsi1phytimerClkSrc)] =
        MsmClkRcg(msm8x53::kCsi1phytimerCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblCsi1phytimerClkSrc, countof(kFtblCsi1phytimerClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCsi2phytimerClkSrc)] =
        MsmClkRcg(msm8x53::kCsi2phytimerCmdRcgr, RcgDividerType::HalfInteger,
                  kFtblCsi2phytimerClkSrc, countof(kFtblCsi2phytimerClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kCryptoClkSrc)] =
        MsmClkRcg(msm8x53::kCryptoCmdRcgr, RcgDividerType::HalfInteger, kFtblCryptoClkSrc,
                  countof(kFtblCryptoClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kGp1ClkSrc)] = MsmClkRcg(
        msm8x53::kGp1CmdRcgr, RcgDividerType::Mnd, kFtblGp1ClkSrc, countof(kFtblGp1ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kGp2ClkSrc)] = MsmClkRcg(
        msm8x53::kGp2CmdRcgr, RcgDividerType::Mnd, kFtblGp2ClkSrc, countof(kFtblGp2ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kGp3ClkSrc)] = MsmClkRcg(
        msm8x53::kGp3CmdRcgr, RcgDividerType::Mnd, kFtblGp3ClkSrc, countof(kFtblGp3ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kByte0ClkSrc)] =
        MsmClkRcg(msm8x53::kByte0CmdRcgr, RcgDividerType::HalfInteger, kFtblByte0ClkSrc,
                  countof(kFtblByte0ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kByte1ClkSrc)] =
        MsmClkRcg(msm8x53::kByte1CmdRcgr, RcgDividerType::HalfInteger, kFtblByte1ClkSrc,
                  countof(kFtblByte1ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kEsc0ClkSrc)] =
        MsmClkRcg(msm8x53::kEsc0CmdRcgr, RcgDividerType::HalfInteger, kFtblEsc0ClkSrc,
                  countof(kFtblEsc0ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kEsc1ClkSrc)] =
        MsmClkRcg(msm8x53::kEsc1CmdRcgr, RcgDividerType::HalfInteger, kFtblEsc1ClkSrc,
                  countof(kFtblEsc1ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kVsyncClkSrc)] =
        MsmClkRcg(msm8x53::kVsyncCmdRcgr, RcgDividerType::HalfInteger, kFtblVsyncClkSrc,
                  countof(kFtblVsyncClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kPdm2ClkSrc)] =
        MsmClkRcg(msm8x53::kPdm2CmdRcgr, RcgDividerType::HalfInteger, kFtblPdm2ClkSrc,
                  countof(kFtblPdm2ClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kRbcprGfxClkSrc)] =
        MsmClkRcg(msm8x53::kRbcprGfxCmdRcgr, RcgDividerType::HalfInteger, kFtblRbcprGfxClkSrc,
                  countof(kFtblRbcprGfxClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1AppsClkSrc)] =
        MsmClkRcg(msm8x53::kSdcc1AppsCmdRcgr, RcgDividerType::Mnd, kFtblSdcc1AppsClkSrc,
                  countof(kFtblSdcc1AppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kSdcc1IceCoreClkSrc)] =
        MsmClkRcg(msm8x53::kSdcc1IceCoreCmdRcgr, RcgDividerType::Mnd, kFtblSdcc1IceCoreClkSrc,
                  countof(kFtblSdcc1IceCoreClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kSdcc2AppsClkSrc)] =
        MsmClkRcg(msm8x53::kSdcc2AppsCmdRcgr, RcgDividerType::Mnd, kFtblSdcc2AppsClkSrc,
                  countof(kFtblSdcc2AppsClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kUsb30MockUtmiClkSrc)] =
        MsmClkRcg(msm8x53::kUsb30MockUtmiCmdRcgr, RcgDividerType::Mnd, kFtblUsb30MockUtmiClkSrc,
                  countof(kFtblUsb30MockUtmiClkSrc)),
    [msm8x53::MsmClkIndex(msm8x53::kUsb3AuxClkSrc)] =
        MsmClkRcg(msm8x53::kUsb3AuxCmdRcgr, RcgDividerType::Mnd, kFtblUsb3AuxClkSrc,
                  countof(kFtblUsb3AuxClkSrc)),
};

static_assert(msm8x53::kRcgClkCount == countof(kMsmClkRcgs),
              "kRcgClkCount must match count of RCG clocks");

constexpr struct msm_clk_voter kMsmClkVoters[] = {
    [msm8x53::MsmClkIndex(msm8x53::kApssAhbClk)] = {.cbcr_reg = msm8x53::kApssAhbCbcr,
                                                    .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                    .bit = (1 << 14)},
    [msm8x53::MsmClkIndex(msm8x53::kApssAxiClk)] = {.cbcr_reg = msm8x53::kApssAxiCbcr,
                                                    .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                    .bit = (1 << 13)},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp1AhbClk)] = {.cbcr_reg = msm8x53::kBlsp1AhbCbcr,
                                                     .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                     .bit = (1 << 10)},
    [msm8x53::MsmClkIndex(msm8x53::kBlsp2AhbClk)] = {.cbcr_reg = msm8x53::kBlsp2AhbCbcr,
                                                     .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                     .bit = (1 << 20)},
    [msm8x53::MsmClkIndex(msm8x53::kBootRomAhbClk)] = {.cbcr_reg = msm8x53::kBootRomAhbCbcr,
                                                       .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                       .bit = (1 << 7)},
    [msm8x53::MsmClkIndex(msm8x53::kCryptoAhbClk)] = {.cbcr_reg = msm8x53::kCryptoAhbCbcr,
                                                      .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                      .bit = (1 << 0)},
    [msm8x53::MsmClkIndex(msm8x53::kCryptoAxiClk)] = {.cbcr_reg = msm8x53::kCryptoAxiCbcr,
                                                      .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                      .bit = (1 << 1)},
    [msm8x53::MsmClkIndex(msm8x53::kCryptoClk)] = {.cbcr_reg = msm8x53::kCryptoCbcr,
                                                   .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                   .bit = (1 << 2)},
    [msm8x53::MsmClkIndex(msm8x53::kQdssDapClk)] = {.cbcr_reg = msm8x53::kQdssDapCbcr,
                                                    .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                    .bit = (1 << 11)},
    [msm8x53::MsmClkIndex(msm8x53::kPrngAhbClk)] = {.cbcr_reg = msm8x53::kPrngAhbCbcr,
                                                    .vote_reg = msm8x53::kApcsClockBranchEnaVote,
                                                    .bit = (1 << 8)},
    [msm8x53::MsmClkIndex(msm8x53::kApssTcuAsyncClk)] = {.cbcr_reg = msm8x53::kApssTcuAsyncCbcr,
                                                         .vote_reg =
                                                             msm8x53::kApcsSmmuClockBranchEnaVote,
                                                         .bit = (1 << 1)},
    [msm8x53::MsmClkIndex(msm8x53::kCppTbuClk)] = {.cbcr_reg = msm8x53::kCppTbuCbcr,
                                                   .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
                                                   .bit = (1 << 14)},
    [msm8x53::MsmClkIndex(msm8x53::kJpegTbuClk)] = {.cbcr_reg = msm8x53::kJpegTbuCbcr,
                                                    .vote_reg =
                                                        msm8x53::kApcsSmmuClockBranchEnaVote,
                                                    .bit = (1 << 10)},
    [msm8x53::MsmClkIndex(msm8x53::kMdpTbuClk)] = {.cbcr_reg = msm8x53::kMdpTbuCbcr,
                                                   .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
                                                   .bit = (1 << 4)},
    [msm8x53::MsmClkIndex(msm8x53::kSmmuCfgClk)] = {.cbcr_reg = msm8x53::kSmmuCfgCbcr,
                                                    .vote_reg =
                                                        msm8x53::kApcsSmmuClockBranchEnaVote,
                                                    .bit = (1 << 12)},
    [msm8x53::MsmClkIndex(msm8x53::kVenusTbuClk)] = {.cbcr_reg = msm8x53::kVenusTbuCbcr,
                                                     .vote_reg =
                                                         msm8x53::kApcsSmmuClockBranchEnaVote,
                                                     .bit = (1 << 5)},
    [msm8x53::MsmClkIndex(msm8x53::kVfe1TbuClk)] = {.cbcr_reg = msm8x53::kVfe1TbuCbcr,
                                                    .vote_reg =
                                                        msm8x53::kApcsSmmuClockBranchEnaVote,
                                                    .bit = (1 << 17)},
    [msm8x53::MsmClkIndex(msm8x53::kVfeTbuClk)] = {.cbcr_reg = msm8x53::kVfeTbuCbcr,
                                                   .vote_reg = msm8x53::kApcsSmmuClockBranchEnaVote,
                                                   .bit = (1 << 9)},
};

}  // namespace

}  // namespace clk

#endif  // ZIRCON_SYSTEM_DEV_CLK_MSM8X53_CLK_MSM8X53_CLK_REGS_H_
