// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BLOCK_DRIVERS_AML_SDMMC_AML_SDMMC_REGS_H_
#define SRC_DEVICES_BLOCK_DRIVERS_AML_SDMMC_AML_SDMMC_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

constexpr uint32_t kAmlSdmmcClockOffset = 0x00;
constexpr uint32_t kAmlSdmmcDelayV2Offset = 0x04;
constexpr uint32_t kAmlSdmmcDelay1Offset = 0x04;
constexpr uint32_t kAmlSdmmcDelay2Offset = 0x08;
constexpr uint32_t kAmlSdmmcAdjustV2Offset = 0x08;
constexpr uint32_t kAmlSdmmcAdjustOffset = 0x0c;
constexpr uint32_t kAmlSdmmcCaloutOffset = 0x10;
constexpr uint32_t kAmlSdmmcCaloutV2Offset = 0x14;

constexpr uint32_t kAmlSdmmcStartOffset = 0x40;
constexpr uint32_t kAmlSdmmcCfgOffset = 0x44;
constexpr uint32_t kAmlSdmmcStatusOffset = 0x48;
constexpr uint32_t kAmlSdmmcIrqEnOffset = 0x4c;
constexpr uint32_t kAmlSdmmcCmdCfgOffset = 0x50;
constexpr uint32_t kAmlSdmmcCmdArgOffset = 0x54;
constexpr uint32_t kAmlSdmmcCmdDatOffset = 0x58;
constexpr uint32_t kAmlSdmmcCmdRespOffset = 0x5c;
constexpr uint32_t kAmlSdmmcCmdResp1Offset = 0x60;
constexpr uint32_t kAmlSdmmcCmdResp2Offset = 0x64;
constexpr uint32_t kAmlSdmmcCmdResp3Offset = 0x68;
constexpr uint32_t kAmlSdmmcCmdBusErrOffset = 0x6c;
constexpr uint32_t kAmlSdmmcCurCfgOffset = 0x70;
constexpr uint32_t kAmlSdmmcCurArgOffset = 0x74;
constexpr uint32_t kAmlSdmmcCurDatOffset = 0x78;
constexpr uint32_t kAmlSdmmcCurRespOffset = 0x7c;
constexpr uint32_t kAmlSdmmcNextCfgOffset = 0x80;
constexpr uint32_t kAmlSdmmcNextArgOffset = 0x84;
constexpr uint32_t kAmlSdmmcNextDatOffset = 0x88;
constexpr uint32_t kAmlSdmmcNextRespOffset = 0x8c;
constexpr uint32_t kAmlSdmmcRxdOffset = 0x90;
constexpr uint32_t kAmlSdmmcTxdOffset = 0x94;
constexpr uint32_t kAmlSdmmcSramDescOffset = 0x200;
constexpr uint32_t kAmlSdmmcPingOffset = 0x400;
constexpr uint32_t kAmlSdmmcPongOffset = 0x600;

class AmlSdmmcClock : public hwreg::RegisterBase<AmlSdmmcClock, uint32_t> {
 public:
  static constexpr uint32_t kCtsOscinClkFreq = 24000000;  // 24MHz
  static constexpr uint32_t kCtsOscinClkSrc = 0;
  static constexpr uint32_t kFClkDiv2Freq = 1000000000;  // 24MHz
  static constexpr uint32_t kFClkDiv2Src = 1;
  //~Min freq attainable with DIV2 Src
  static constexpr uint32_t kFClkDiv2MinFreq = 20000000;  // 20MHz
  static constexpr uint32_t kDefaultClkSrc = 0;           // 24MHz
  static constexpr uint32_t kDefaultClkDiv = 60;          // Defaults to 400KHz
  static constexpr uint32_t kClkPhase0Degrees = 0;
  static constexpr uint32_t kClkPhase90Degrees = 1;
  static constexpr uint32_t kClkPhase180Degrees = 2;
  static constexpr uint32_t kClkPhase270Degrees = 3;
  static constexpr uint32_t kDefaultClkCorePhase = kClkPhase180Degrees;
  static constexpr uint32_t kDefaultClkTxPhase = kClkPhase0Degrees;
  static constexpr uint32_t kDefaultClkRxPhase = kClkPhase0Degrees;
  static constexpr uint32_t kMaxClkDiv = 63;
  static constexpr uint32_t kMaxClkPhase = 3;
  static constexpr uint32_t kMaxDelay = 63;
  static constexpr uint32_t kMaxDelayV2 = 15;

  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcClock>(kAmlSdmmcClockOffset); }

  DEF_FIELD(5, 0, cfg_div);
  DEF_FIELD(7, 6, cfg_src);
  DEF_FIELD(9, 8, cfg_co_phase);
  DEF_FIELD(11, 10, cfg_tx_phase);
  DEF_FIELD(13, 12, cfg_rx_phase);
  DEF_FIELD(15, 14, cfg_sram_pd);
  DEF_FIELD(21, 16, cfg_tx_delay);
  DEF_FIELD(27, 22, cfg_rx_delay);
  DEF_BIT(28, cfg_always_on);
  DEF_BIT(29, cfg_irq_sdio_sleep);
  DEF_BIT(30, cfg_irq_sdio_sleep_ds);
  DEF_BIT(31, cfg_nand);
};

class AmlSdmmcCfg : public hwreg::RegisterBase<AmlSdmmcCfg, uint32_t> {
 public:
  static constexpr uint32_t kBusWidth1Bit = 0;
  static constexpr uint32_t kBusWidth4Bit = 1;
  static constexpr uint32_t kBusWidth8Bit = 2;

  static constexpr uint32_t kDefaultBlkLen = 9;       // 512 bytes
  static constexpr uint32_t kMaxBlkLen = 9;           // 512 bytes
  static constexpr uint32_t kDefaultRespTimeout = 8;  // 256 core clock cycles
  static constexpr uint32_t kDefaultRcCc = 4;         // 16 core clock cycles

  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCfg>(kAmlSdmmcCfgOffset); }

  DEF_FIELD(1, 0, bus_width);
  DEF_BIT(2, ddr);
  DEF_BIT(3, dc_ugt);
  DEF_FIELD(7, 4, blk_len);
  DEF_FIELD(11, 8, resp_timeout);
  DEF_FIELD(15, 12, rc_cc);
  DEF_BIT(16, out_fall);
  DEF_BIT(17, blk_gap_ip);
  DEF_BIT(18, sdclk_always_on);
  DEF_BIT(19, ignore_owner);
  DEF_BIT(20, chk_ds);
  DEF_BIT(21, cmd_low);
  DEF_BIT(22, stop_clk);
  DEF_BIT(23, auto_clk);
  DEF_BIT(24, txd_add_err);
  DEF_BIT(25, txd_retry);
  DEF_BIT(26, irq_ds);
  DEF_BIT(27, err_abort);
  DEF_FIELD(31, 28, ip_txd_adj);
};

class AmlSdmmcStatus : public hwreg::RegisterBase<AmlSdmmcStatus, uint32_t> {
 public:
  static constexpr uint32_t kClearStatus = 0x7fff;
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcStatus>(kAmlSdmmcStatusOffset); }

  DEF_FIELD(7, 0, rxd_err);
  DEF_BIT(8, txd_err);
  DEF_BIT(9, desc_err);
  DEF_BIT(10, resp_err);
  DEF_BIT(11, resp_timeout);
  DEF_BIT(12, desc_timeout);
  DEF_BIT(13, end_of_chain);
  DEF_BIT(14, resp_status);
  DEF_BIT(15, irq_sdio);
  DEF_FIELD(23, 16, dat_i);
  DEF_BIT(24, cmd_i);
  DEF_BIT(25, ds);
  DEF_FIELD(29, 26, bus_fsm);
  DEF_BIT(30, desc_busy);
  DEF_BIT(31, core_busy);
};

class AmlSdmmcCmdCfg : public hwreg::RegisterBase<AmlSdmmcCmdCfg, uint32_t> {
 public:
  static constexpr uint32_t kDefaultCmdTimeout = 0xc;  // 2^12 ms.
  static constexpr uint32_t kMaxBlockSize = 512;       // 9 bits
  static constexpr uint32_t kMaxBlockCount = (1 << 9) - 1;  // 9 bits
  static constexpr uint32_t kDataAddrAlignment = 4;

  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCmdCfg>(kAmlSdmmcCmdCfgOffset); }

  void set_length(uint32_t length) {
    if (length >= AmlSdmmcCmdCfg::kMaxBlockSize) {
      // Controller supports 512bytes and uses 0 to denote that.
      length = 0;
    }
    set_len(length);
  }
  DEF_FIELD(8, 0, len);
  DEF_BIT(9, block_mode);
  DEF_BIT(10, r1b);
  DEF_BIT(11, end_of_chain);
  DEF_FIELD(15, 12, timeout);
  DEF_BIT(16, no_resp);
  DEF_BIT(17, no_cmd);
  DEF_BIT(18, data_io);
  DEF_BIT(19, data_wr);
  DEF_BIT(20, resp_no_crc);
  DEF_BIT(21, resp_128);
  DEF_BIT(22, resp_num);
  DEF_BIT(23, data_num);
  DEF_FIELD(29, 24, cmd_idx);
  DEF_BIT(30, error);
  DEF_BIT(31, owner);
};

class AmlSdmmcIrqEn : public hwreg::RegisterBase<AmlSdmmcIrqEn, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcIrqEn>(kAmlSdmmcIrqEnOffset); }
};

class AmlSdmmcCmdResp : public hwreg::RegisterBase<AmlSdmmcCmdResp, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCmdResp>(kAmlSdmmcCmdRespOffset); }
};

class AmlSdmmcCmdResp1 : public hwreg::RegisterBase<AmlSdmmcCmdResp1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCmdResp1>(kAmlSdmmcCmdResp1Offset); }
};

class AmlSdmmcCmdResp2 : public hwreg::RegisterBase<AmlSdmmcCmdResp2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCmdResp2>(kAmlSdmmcCmdResp2Offset); }
};

class AmlSdmmcCmdResp3 : public hwreg::RegisterBase<AmlSdmmcCmdResp3, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCmdResp3>(kAmlSdmmcCmdResp3Offset); }
};

class AmlSdmmcDelayV2 : public hwreg::RegisterBase<AmlSdmmcDelayV2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcDelayV2>(kAmlSdmmcDelayV2Offset); }

  DEF_FIELD(3, 0, dly_0);
  DEF_FIELD(7, 4, dly_1);
  DEF_FIELD(11, 8, dly_2);
  DEF_FIELD(15, 12, dly_3);
  DEF_FIELD(19, 16, dly_4);
  DEF_FIELD(23, 20, dly_5);
  DEF_FIELD(27, 24, dly_6);
  DEF_FIELD(31, 28, dly_7);
};

class AmlSdmmcDelay1 : public hwreg::RegisterBase<AmlSdmmcDelay1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcDelay1>(kAmlSdmmcDelay1Offset); }

  DEF_FIELD(5, 0, dly_0);
  DEF_FIELD(11, 6, dly_1);
  DEF_FIELD(17, 12, dly_2);
  DEF_FIELD(23, 18, dly_3);
  DEF_FIELD(29, 24, dly_4);
};

class AmlSdmmcDelay2 : public hwreg::RegisterBase<AmlSdmmcDelay2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcDelay2>(kAmlSdmmcDelay2Offset); }

  DEF_FIELD(5, 0, dly_5);
  DEF_FIELD(11, 6, dly_6);
  DEF_FIELD(17, 12, dly_7);
  DEF_FIELD(23, 18, dly_8);
  DEF_FIELD(29, 24, dly_9);
};

class AmlSdmmcCalout : public hwreg::RegisterBase<AmlSdmmcCalout, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCalout>(kAmlSdmmcCaloutOffset); }
};

class AmlSdmmcCmdArg : public hwreg::RegisterBase<AmlSdmmcCmdArg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCmdArg>(kAmlSdmmcCmdArgOffset); }
};

class AmlSdmmcCmdDat : public hwreg::RegisterBase<AmlSdmmcCmdDat, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCmdDat>(kAmlSdmmcCmdDatOffset); }
};

class AmlSdmmcCmdBusErr : public hwreg::RegisterBase<AmlSdmmcCmdBusErr, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCmdBusErr>(kAmlSdmmcCmdBusErrOffset); }
};

class AmlSdmmcCurCfg : public hwreg::RegisterBase<AmlSdmmcCurCfg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCurCfg>(kAmlSdmmcCurCfgOffset); }
};

class AmlSdmmcCurArg : public hwreg::RegisterBase<AmlSdmmcCurArg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCurArg>(kAmlSdmmcCurArgOffset); }
};

class AmlSdmmcCurDat : public hwreg::RegisterBase<AmlSdmmcCurDat, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCurDat>(kAmlSdmmcCurDatOffset); }
};

class AmlSdmmcCurResp : public hwreg::RegisterBase<AmlSdmmcCurResp, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcCurResp>(kAmlSdmmcCurRespOffset); }
};

class AmlSdmmcNextCfg : public hwreg::RegisterBase<AmlSdmmcNextCfg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcNextCfg>(kAmlSdmmcNextCfgOffset); }
};

class AmlSdmmcNextArg : public hwreg::RegisterBase<AmlSdmmcNextArg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcNextArg>(kAmlSdmmcNextArgOffset); }
};

class AmlSdmmcNextDat : public hwreg::RegisterBase<AmlSdmmcNextDat, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcNextDat>(kAmlSdmmcNextDatOffset); }
};

class AmlSdmmcNextResp : public hwreg::RegisterBase<AmlSdmmcNextResp, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcNextResp>(kAmlSdmmcNextRespOffset); }
};

class AmlSdmmcStart : public hwreg::RegisterBase<AmlSdmmcStart, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcStart>(kAmlSdmmcStartOffset); }

  DEF_BIT(0, desc_int);
  DEF_BIT(1, desc_busy);
  DEF_FIELD(31, 2, desc_addr);
};

class AmlSdmmcAdjust : public hwreg::RegisterBase<AmlSdmmcAdjust, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcAdjust>(kAmlSdmmcAdjustOffset); }
  DEF_FIELD(11, 8, cali_sel);
  DEF_BIT(12, cali_enable);
  DEF_BIT(13, adj_fixed);
  DEF_BIT(14, cali_rise);
  DEF_BIT(15, ds_enable);
  DEF_FIELD(21, 16, adj_delay);
  DEF_BIT(22, adj_auto);
};

class AmlSdmmcAdjustV2 : public hwreg::RegisterBase<AmlSdmmcAdjustV2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdmmcAdjustV2>(kAmlSdmmcAdjustV2Offset); }
  DEF_FIELD(3, 0, dly_8);
  DEF_FIELD(7, 4, dly_9);
  DEF_FIELD(11, 8, cali_sel);
  DEF_BIT(12, cali_enable);
  DEF_BIT(13, adj_fixed);
  DEF_BIT(14, cali_rise);
  DEF_BIT(15, ds_enable);
  DEF_FIELD(21, 16, adj_delay);
  DEF_BIT(22, adj_auto);
};

#endif  // SRC_DEVICES_BLOCK_DRIVERS_AML_SDMMC_AML_SDMMC_REGS_H_
