// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_DHUB_REGS_H_
#define SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_DHUB_REGS_H_

#include <zircon/types.h>

#include <hwreg/bitfields.h>

// AIO 64b dHub Registers

// aio64bDhub_mem at 0 is 12.5 KB of TCM.

// aio64bDhub_dHub0_dHub_SemaHub_Query (HUB query) at 0x1'0000
// aio64bDhub_dHub0_dHub_HBO_FiFoCtl_Query (FIFO query) at 0x1'0500.

// aio64bDhub_dHub0_dHub_SemaHub_cell0_CFG (hub == true) and friends (cell 0 to 31) at 0x1'0100.
// aio64bDhub_dHub0_dHub_HBO_FiFoCtl_cell0_CFG and friends (cell 0 to 31) at 0x1'0600.
struct cell_CFG : public hwreg::RegisterBase<cell_CFG, uint32_t> {
  DEF_FIELD(15, 0, DEPTH);
  static auto Get(bool hub, uint32_t id) {
    return hwreg::RegisterAddr<cell_CFG>((hub ? 0x1'0100 : 0x1'0600) + id * 0x18);
  }
};

struct cell_INTR0_mask : public hwreg::RegisterBase<cell_INTR0_mask, uint32_t> {
  DEF_BIT(3, almostFull);
  DEF_BIT(2, almostEmpty);
  DEF_BIT(1, full);
  DEF_BIT(0, empty);
  static auto Get(bool hub, uint32_t id) {
    return hwreg::RegisterAddr<cell_INTR0_mask>((hub ? 0x1'0104 : 0x1'0604) + id * 0x18);
  }
};

struct cell_INTR1_mask : public hwreg::RegisterBase<cell_INTR1_mask, uint32_t> {
  DEF_BIT(3, almostFull);
  DEF_BIT(2, almostEmpty);
  DEF_BIT(1, full);
  DEF_BIT(0, empty);
  static auto Get(bool hub, uint32_t id) {
    return hwreg::RegisterAddr<cell_INTR1_mask>((hub ? 0x1'0108 : 0x1'0608) + id * 0x18);
  }
};

struct cell_INTR2_mask : public hwreg::RegisterBase<cell_INTR2_mask, uint32_t> {
  DEF_BIT(3, almostFull);
  DEF_BIT(2, almostEmpty);
  DEF_BIT(1, full);
  DEF_BIT(0, empty);
  static auto Get(bool hub, uint32_t id) {
    return hwreg::RegisterAddr<cell_INTR2_mask>((hub ? 0x1'010c : 0x1'060c) + id * 0x18);
  }
};

struct cell_mask : public hwreg::RegisterBase<cell_mask, uint32_t> {
  DEF_BIT(1, emp);
  DEF_BIT(0, full);
  static auto Get(bool hub, uint32_t id) {
    return hwreg::RegisterAddr<cell_mask>((hub ? 0x1'0110 : 0x1'0610) + id * 0x18);
  }
};

struct cell_thresh : public hwreg::RegisterBase<cell_thresh, uint32_t> {
  DEF_FIELD(3, 2, aEmpty);
  DEF_FIELD(1, 0, aFull);
  static auto Get(bool hub, uint32_t id) {
    return hwreg::RegisterAddr<cell_thresh>((hub ? 0x1'0114 : 0x1'0614) + id * 0x18);
  }
};

// aio64bDhub_dHub0_dHub_SemaHub_PUSH (hub == true) at 0x1'0400.
// aio64bDhub_dHub0_dHub_HBO_FiFoCtl_PUSH at 0x1'0900.
struct PUSH : public hwreg::RegisterBase<PUSH, uint32_t> {
  DEF_FIELD(15, 8, delta);
  DEF_FIELD(7, 0, ID);
  static auto Get(bool hub) { return hwreg::RegisterAddr<PUSH>(hub ? 0x1'0400 : 0x1'0900); }
};

// aio64bDhub_dHub0_dHub_SemaHub_POP (hub == true) at 0x1'0404
// aio64bDhub_dHub0_dHub_HBO_FiFoCtl_POP at 0x1'0904.
struct POP : public hwreg::RegisterBase<POP, uint32_t> {
  DEF_FIELD(15, 8, delta);
  DEF_FIELD(7, 0, ID);
  static auto Get(bool hub) { return hwreg::RegisterAddr<POP>(hub ? 0x1'0404 : 0x1'0904); }
};

struct full : public hwreg::RegisterBase<full, uint32_t> {
  DEF_FIELD(31, 0, ST);
  static auto Get(bool hub) { return hwreg::RegisterAddr<full>(hub ? 0x1'040c : 0x1'090c); }
};

// aio64bDhub_dHub0_dHub_empty at 0x1'0408.
// aio64bDhub_dHub0_dHub_full at 0x1'040c.
// aio64bDhub_dHub0_dHub_almostEmpty 0x1'0410.
// aio64bDhub_dHub0_dHub_almostFull 0x1'0414.

// aio64bDhub_dHub0_dHub_HBO_FiFoCtl_empty at 0x1'0908.
// aio64bDhub_dHub0_dHub_HBO_FiFoCtl_full at 0x1'090c.
// aio64bDhub_dHub0_dHub_HBO_FiFoCtl_almostEmpty at 0x1'0910.
// aio64bDhub_dHub0_dHub_HBO_FiFoCtl_almostFull at 0x1'0914.

// Start of aio64bDhub_dHub0_dHub_HBO_FiFo0_CFG and friends (Fifo0 to Fifo31).
struct FiFo_CFG : public hwreg::RegisterBase<FiFo_CFG, uint32_t> {
  DEF_FIELD(19, 0, BASE);
  static auto Get(uint32_t id) { return hwreg::RegisterAddr<FiFo_CFG>(0x1'0a00 + id * 0x10); }
};

struct FiFo_START : public hwreg::RegisterBase<FiFo_START, uint32_t> {
  DEF_BIT(0, EN);
  static auto Get(uint32_t id) { return hwreg::RegisterAddr<FiFo_START>(0x1'0a04 + id * 0x10); }
};

struct FiFo_CLEAR : public hwreg::RegisterBase<FiFo_CLEAR, uint32_t> {
  DEF_BIT(0, EN);
  static auto Get(uint32_t id) { return hwreg::RegisterAddr<FiFo_CLEAR>(0x1'0a08 + id * 0x10); }
};

struct FiFo_FLUSH : public hwreg::RegisterBase<FiFo_FLUSH, uint32_t> {
  DEF_BIT(0, EN);
  static auto Get(uint32_t id) { return hwreg::RegisterAddr<FiFo_FLUSH>(0x1'0a0c + id * 0x10); }
};
// End of aio64bDhub_dHub0_dHub_HBO_FiFo0_CFG and friends (Fifo0 to Fifo31).

struct HBO_BUSY : public hwreg::RegisterBase<HBO_BUSY, uint32_t> {
  DEF_FIELD(31, 0, ST);
  static auto Get() { return hwreg::RegisterAddr<HBO_BUSY>(0x1'0c00); }
};

// Start of aio64bDhub_dHub0_dHub_channelCtl0_CFG and friends (ChannelCtl 0 to ChannelCtl 15).
struct channelCtl_CFG : public hwreg::RegisterBase<channelCtl_CFG, uint32_t> {
  DEF_BIT(8, vScan);
  DEF_BIT(7, hScan);
  DEF_BIT(6, intrCtl);
  DEF_BIT(5, selfLoop);
  DEF_BIT(4, QoS);
  DEF_FIELD(3, 0, MTU);  // bytes = 2 ^ MTU x 8.
  static auto Get(uint32_t id) { return hwreg::RegisterAddr<channelCtl_CFG>(0x1'0d00 + id * 0x24); }
};

struct channelCtl_ROB_MAP : public hwreg::RegisterBase<channelCtl_ROB_MAP, uint32_t> {
  DEF_FIELD(3, 0, ID);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<channelCtl_ROB_MAP>(0x1'0d04 + id * 0x24);
  }
};

struct channelCtl_AWQOS : public hwreg::RegisterBase<channelCtl_AWQOS, uint32_t> {
  DEF_FIELD(7, 4, HI);
  DEF_FIELD(3, 0, LO);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<channelCtl_AWQOS>(0x1'0d08 + id * 0x24);
  }
};

struct channelCtl_ARQOS : public hwreg::RegisterBase<channelCtl_ARQOS, uint32_t> {
  DEF_FIELD(7, 4, HI);
  DEF_FIELD(3, 0, LO);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<channelCtl_ARQOS>(0x1'0d0c + id * 0x24);
  }
};

struct channelCtl_AWPARAMS : public hwreg::RegisterBase<channelCtl_AWPARAMS, uint32_t> {
  DEF_BIT(14, USER_HI_EN);
  DEF_FIELD(13, 11, CACHE);
  DEF_FIELD(10, 5, USER);
  DEF_FIELD(4, 2, PROT);
  DEF_FIELD(1, 0, LOCK);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<channelCtl_AWPARAMS>(0x1'0d10 + id * 0x24);
  }
};

struct channelCtl_ARPARAMS : public hwreg::RegisterBase<channelCtl_ARPARAMS, uint32_t> {
  DEF_BIT(15, USER_HI_EN);
  DEF_FIELD(14, 11, CACHE);
  DEF_FIELD(10, 5, USER);
  DEF_FIELD(4, 2, PROT);
  DEF_FIELD(1, 0, LOCK);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<channelCtl_ARPARAMS>(0x1'0d14 + id * 0x24);
  }
};

struct channelCtl_START : public hwreg::RegisterBase<channelCtl_START, uint32_t> {
  DEF_BIT(0, EN);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<channelCtl_START>(0x1'0d18 + id * 0x24);
  }
};

struct channelCtl_CLEAR : public hwreg::RegisterBase<channelCtl_CLEAR, uint32_t> {
  DEF_BIT(0, EN);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<channelCtl_CLEAR>(0x1'0d1c + id * 0x24);
  }
};

struct channelCtl_FLUSH : public hwreg::RegisterBase<channelCtl_FLUSH, uint32_t> {
  DEF_BIT(0, EN);
  static auto Get(uint32_t id) {
    return hwreg::RegisterAddr<channelCtl_FLUSH>(0x1'0d20 + id * 0x24);
  }
};
// End of aio64bDhub_dHub0_dHub_channelCtl0_CFG and friends (ChannelCtl 0 to ChannelCtl 15).

// aio64bDhub_dHub0_dHub_BUSY.
struct BUSY : public hwreg::RegisterBase<BUSY, uint32_t> {
  DEF_FIELD(15, 0, ST);
  static auto Get() { return hwreg::RegisterAddr<BUSY>(0x1'0f40); }
};

// aio64bDhub_dHub0_dHub_PENDING.
struct PENDING : public hwreg::RegisterBase<PENDING, uint32_t> {
  DEF_FIELD(15, 0, ST);
  static auto Get() { return hwreg::RegisterAddr<PENDING>(0x1'0f44); }
};

struct CommandAddress : public hwreg::RegisterBase<CommandAddress, uint32_t> {
  DEF_FIELD(31, 0, addr);
  static auto Get(uint32_t offset) {
    return hwreg::RegisterAddr<CommandAddress>(0x0'0000 + offset);
  }
};

struct CommandHeader : public hwreg::RegisterBase<CommandHeader, uint32_t> {
  DEF_BIT(31, qosSel);
  DEF_BIT(30, disSem);
  DEF_BIT(29, ovrdQos);
  DEF_BIT(28, interrupt);
  DEF_FIELD(27, 23, updSemId);
  DEF_FIELD(22, 18, chkSemId);
  DEF_BIT(17, semOpMTU);
  DEF_BIT(16, sizeMTU);
  DEF_FIELD(15, 0, size);
  static auto Get(uint32_t offset) { return hwreg::RegisterAddr<CommandHeader>(0x0'0004 + offset); }
};

#endif  // SRC_DEVICES_LIB_AS370_INCLUDE_SOC_AS370_AS370_DHUB_REGS_H_
