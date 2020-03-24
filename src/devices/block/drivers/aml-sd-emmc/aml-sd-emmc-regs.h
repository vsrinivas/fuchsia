// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_BLOCK_DRIVERS_AML_SD_EMMC_AML_SD_EMMC_REGS_H_
#define SRC_STORAGE_BLOCK_DRIVERS_AML_SD_EMMC_AML_SD_EMMC_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

constexpr uint32_t kAmlSdEmmcClockOffset = 0x00;
constexpr uint32_t kAmlSdEmmcDelayV2Offset = 0x04;
constexpr uint32_t kAmlSdEmmcDelay1Offset = 0x04;
constexpr uint32_t kAmlSdEmmcDelay2Offset = 0x08;
constexpr uint32_t kAmlSdEmmcAdjustV2Offset = 0x08;
constexpr uint32_t kAmlSdEmmcAdjustOffset = 0x0c;
constexpr uint32_t kAmlSdEmmcCaloutOffset = 0x10;
constexpr uint32_t kAmlSdEmmcCaloutV2Offset = 0x14;

constexpr uint32_t kAmlSdEmmcStartOffset = 0x40;
constexpr uint32_t kAmlSdEmmcCfgOffset = 0x44;
constexpr uint32_t kAmlSdEmmcStatusOffset = 0x48;
constexpr uint32_t kAmlSdEmmcIrqEnOffset = 0x4c;
constexpr uint32_t kAmlSdEmmcCmdCfgOffset = 0x50;
constexpr uint32_t kAmlSdEmmcCmdArgOffset = 0x54;
constexpr uint32_t kAmlSdEmmcCmdDatOffset = 0x58;
constexpr uint32_t kAmlSdEmmcCmdRespOffset = 0x5c;
constexpr uint32_t kAmlSdEmmcCmdResp1Offset = 0x60;
constexpr uint32_t kAmlSdEmmcCmdResp2Offset = 0x64;
constexpr uint32_t kAmlSdEmmcCmdResp3Offset = 0x68;
constexpr uint32_t kAmlSdEmmcCmdBusErrOffset = 0x6c;
constexpr uint32_t kAmlSdEmmcCurCfgOffset = 0x70;
constexpr uint32_t kAmlSdEmmcCurArgOffset = 0x74;
constexpr uint32_t kAmlSdEmmcCurDatOffset = 0x78;
constexpr uint32_t kAmlSdEmmcCurRespOffset = 0x7c;
constexpr uint32_t kAmlSdEmmcNextCfgOffset = 0x80;
constexpr uint32_t kAmlSdEmmcNextArgOffset = 0x84;
constexpr uint32_t kAmlSdEmmcNextDatOffset = 0x88;
constexpr uint32_t kAmlSdEmmcNextRespOffset = 0x8c;
constexpr uint32_t kAmlSdEmmcRxdOffset = 0x90;
constexpr uint32_t kAmlSdEmmcTxdOffset = 0x94;
constexpr uint32_t kAmlSdEmmcSramDescOffset = 0x200;
constexpr uint32_t kAmlSdEmmcPingOffset = 0x400;
constexpr uint32_t kAmlSdEmmcPongOffset = 0x600;

class AmlSdEmmcClock : public hwreg::RegisterBase<AmlSdEmmcClock, uint32_t> {
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

  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcClock>(kAmlSdEmmcClockOffset); }

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

class AmlSdEmmcCfg : public hwreg::RegisterBase<AmlSdEmmcCfg, uint32_t> {
 public:
  static constexpr uint32_t kBusWidth1Bit = 0;
  static constexpr uint32_t kBusWidth4Bit = 1;
  static constexpr uint32_t kBusWidth8Bit = 2;

  static constexpr uint32_t kDefaultBlkLen = 9;       // 512 bytes
  static constexpr uint32_t kDefaultRespTimeout = 8;  // 256 core clock cycles
  static constexpr uint32_t kDefaultRcCc = 4;         // 16 core clock cycles

  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCfg>(kAmlSdEmmcCfgOffset); }

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

class AmlSdEmmcStatus : public hwreg::RegisterBase<AmlSdEmmcStatus, uint32_t> {
 public:
  static constexpr uint32_t kClearStatus = 0x7fff;
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcStatus>(kAmlSdEmmcStatusOffset); }

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

class AmlSdEmmcCmdCfg : public hwreg::RegisterBase<AmlSdEmmcCmdCfg, uint32_t> {
 public:
  static constexpr uint32_t kDefaultCmdTimeout = 0xc;  // 2^12 ms.
  static constexpr uint32_t kMaxBlockSize = 512;       // 9 bits

  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCmdCfg>(kAmlSdEmmcCmdCfgOffset); }

  void set_length(uint32_t length) {
    if (length >= AmlSdEmmcCmdCfg::kMaxBlockSize) {
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

class AmlSdEmmcIrqEn : public hwreg::RegisterBase<AmlSdEmmcIrqEn, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcIrqEn>(kAmlSdEmmcIrqEnOffset); }
};

class AmlSdEmmcCmdResp : public hwreg::RegisterBase<AmlSdEmmcCmdResp, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCmdResp>(kAmlSdEmmcCmdRespOffset); }
};

class AmlSdEmmcCmdResp1 : public hwreg::RegisterBase<AmlSdEmmcCmdResp1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCmdResp1>(kAmlSdEmmcCmdResp1Offset); }
};

class AmlSdEmmcCmdResp2 : public hwreg::RegisterBase<AmlSdEmmcCmdResp2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCmdResp2>(kAmlSdEmmcCmdResp2Offset); }
};

class AmlSdEmmcCmdResp3 : public hwreg::RegisterBase<AmlSdEmmcCmdResp3, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCmdResp3>(kAmlSdEmmcCmdResp3Offset); }
};

class AmlSdEmmcDelayV2 : public hwreg::RegisterBase<AmlSdEmmcDelayV2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcDelayV2>(kAmlSdEmmcDelayV2Offset); }

  DEF_FIELD(3, 0, dly_0);
  DEF_FIELD(7, 4, dly_1);
  DEF_FIELD(11, 8, dly_2);
  DEF_FIELD(15, 12, dly_3);
  DEF_FIELD(19, 16, dly_4);
  DEF_FIELD(23, 20, dly_5);
  DEF_FIELD(27, 24, dly_6);
  DEF_FIELD(31, 28, dly_7);
};

class AmlSdEmmcDelay1 : public hwreg::RegisterBase<AmlSdEmmcDelay1, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcDelay1>(kAmlSdEmmcDelay1Offset); }

  DEF_FIELD(5, 0, dly_0);
  DEF_FIELD(11, 6, dly_1);
  DEF_FIELD(17, 12, dly_2);
  DEF_FIELD(23, 18, dly_3);
  DEF_FIELD(29, 24, dly_4);
};

class AmlSdEmmcDelay2 : public hwreg::RegisterBase<AmlSdEmmcDelay2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcDelay2>(kAmlSdEmmcDelay2Offset); }

  DEF_FIELD(5, 0, dly_5);
  DEF_FIELD(11, 6, dly_6);
  DEF_FIELD(17, 12, dly_7);
  DEF_FIELD(23, 18, dly_8);
  DEF_FIELD(29, 24, dly_9);
};

class AmlSdEmmcCalout : public hwreg::RegisterBase<AmlSdEmmcCalout, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCalout>(kAmlSdEmmcCaloutOffset); }
};

class AmlSdEmmcCmdArg : public hwreg::RegisterBase<AmlSdEmmcCmdArg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCmdArg>(kAmlSdEmmcCmdArgOffset); }
};

class AmlSdEmmcCmdDat : public hwreg::RegisterBase<AmlSdEmmcCmdDat, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCmdDat>(kAmlSdEmmcCmdDatOffset); }
};

class AmlSdEmmcCmdBusErr : public hwreg::RegisterBase<AmlSdEmmcCmdBusErr, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCmdBusErr>(kAmlSdEmmcCmdBusErrOffset); }
};

class AmlSdEmmcCurCfg : public hwreg::RegisterBase<AmlSdEmmcCurCfg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCurCfg>(kAmlSdEmmcCurCfgOffset); }
};

class AmlSdEmmcCurArg : public hwreg::RegisterBase<AmlSdEmmcCurArg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCurArg>(kAmlSdEmmcCurArgOffset); }
};

class AmlSdEmmcCurDat : public hwreg::RegisterBase<AmlSdEmmcCurDat, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCurDat>(kAmlSdEmmcCurDatOffset); }
};

class AmlSdEmmcCurResp : public hwreg::RegisterBase<AmlSdEmmcCurResp, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcCurResp>(kAmlSdEmmcCurRespOffset); }
};

class AmlSdEmmcNextCfg : public hwreg::RegisterBase<AmlSdEmmcNextCfg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcNextCfg>(kAmlSdEmmcNextCfgOffset); }
};

class AmlSdEmmcNextArg : public hwreg::RegisterBase<AmlSdEmmcNextArg, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcNextArg>(kAmlSdEmmcNextArgOffset); }
};

class AmlSdEmmcNextDat : public hwreg::RegisterBase<AmlSdEmmcNextDat, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcNextDat>(kAmlSdEmmcNextDatOffset); }
};

class AmlSdEmmcNextResp : public hwreg::RegisterBase<AmlSdEmmcNextResp, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcNextResp>(kAmlSdEmmcNextRespOffset); }
};

class AmlSdEmmcStart : public hwreg::RegisterBase<AmlSdEmmcStart, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcStart>(kAmlSdEmmcStartOffset); }

  DEF_BIT(0, desc_int);
  DEF_BIT(1, desc_busy);
  DEF_FIELD(31, 2, desc_addr);
};

class AmlSdEmmcAdjust : public hwreg::RegisterBase<AmlSdEmmcAdjust, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcAdjust>(kAmlSdEmmcAdjustOffset); }
  DEF_FIELD(11, 8, cali_sel);
  DEF_BIT(12, cali_enable);
  DEF_BIT(13, adj_fixed);
  DEF_BIT(14, cali_rise);
  DEF_BIT(15, ds_enable);
  DEF_FIELD(21, 16, adj_delay);
  DEF_BIT(22, adj_auto);
};

class AmlSdEmmcAdjustV2 : public hwreg::RegisterBase<AmlSdEmmcAdjustV2, uint32_t> {
 public:
  static auto Get() { return hwreg::RegisterAddr<AmlSdEmmcAdjustV2>(kAmlSdEmmcAdjustV2Offset); }
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

#endif  // SRC_STORAGE_BLOCK_DRIVERS_AML_SD_EMMC_AML_SD_EMMC_REGS_H_
