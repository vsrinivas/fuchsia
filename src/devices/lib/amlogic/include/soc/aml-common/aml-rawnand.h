// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_RAWNAND_H_
#define SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_RAWNAND_H_

static constexpr uint32_t P_NAND_CMD = (0x00);
static constexpr uint32_t P_NAND_CFG = (0x04);
static constexpr uint32_t P_NAND_DADR = (0x08);
static constexpr uint32_t P_NAND_IADR = (0x0c);
static constexpr uint32_t P_NAND_BUF = (0x10);
static constexpr uint32_t P_NAND_INFO = (0x14);
static constexpr uint32_t P_NAND_DC = (0x18);
static constexpr uint32_t P_NAND_ADR = (0x1c);
static constexpr uint32_t P_NAND_DL = (0x20);
static constexpr uint32_t P_NAND_DH = (0x24);
static constexpr uint32_t P_NAND_CADR = (0x28);
static constexpr uint32_t P_NAND_SADR = (0x2c);
static constexpr uint32_t P_NAND_PINS = (0x30);
static constexpr uint32_t P_NAND_VER = (0x38);

static constexpr uint32_t AML_CMD_DRD = (0x8 << 14);
static constexpr uint32_t AML_CMD_IDLE = (0xc << 14);
static constexpr uint32_t AML_CMD_DWR = (0x4 << 14);
static constexpr uint32_t AML_CMD_CLE = (0x5 << 14);
static constexpr uint32_t AML_CMD_ALE = (0x6 << 14);
static constexpr uint32_t AML_CMD_ADL = ((0 << 16) | (3 << 20));
static constexpr uint32_t AML_CMD_ADH = ((1 << 16) | (3 << 20));
static constexpr uint32_t AML_CMD_AIL = ((2 << 16) | (3 << 20));
static constexpr uint32_t AML_CMD_AIH = ((3 << 16) | (3 << 20));
static constexpr uint32_t AML_CMD_SEED = ((8 << 16) | (3 << 20));
static constexpr uint32_t AML_CMD_M2N = ((0 << 17) | (2 << 20));
static constexpr uint32_t AML_CMD_N2M = ((1 << 17) | (2 << 20));
static constexpr uint32_t AML_CMD_RB = (1 << 20);
static constexpr uint32_t AML_CMD_IO6 = ((0xb << 10) | (1 << 18));

static constexpr uint32_t NAND_TWB_TIME_CYCLE = 10;

static constexpr uint32_t CMDRWGEN(uint32_t cmd_dir, int ran, int bch, uint32_t value,
                                   uint32_t pagesize, uint32_t pages) {
  return ((cmd_dir) | (ran) << 19 | (bch) << 14 | (value) << 13 | ((pagesize)&0x7f) << 6 |
          ((pages)&0x3f));
}

static constexpr uint32_t GENCMDDADDRL(uint32_t adl, zx_paddr_t addr) {
  return static_cast<uint32_t>((adl) | ((addr)&0xffff));
}

static constexpr uint32_t GENCMDDADDRH(uint32_t adh, zx_paddr_t addr) {
  return static_cast<uint32_t>((adh) | (((addr) >> 16) & 0xffff));
}

static constexpr uint32_t GENCMDIADDRL(uint32_t ail, zx_paddr_t addr) {
  return static_cast<uint32_t>((ail) | ((addr)&0xffff));
}

static constexpr uint32_t GENCMDIADDRH(uint32_t aih, zx_paddr_t addr) {
  return static_cast<uint32_t>((aih) | (((addr) >> 16) & 0xffff));
}

static constexpr uint32_t AML_ECC_UNCORRECTABLE_CNT = 0x3f;

static constexpr int32_t ECC_CHECK_RETURN_FF = -1;

static constexpr uint32_t CMD_FINISH_TIMEOUT_MS = 1000;

static constexpr uint32_t AML_ECC_NONE = 0;
// bch8 with ecc page size of 512B.
static constexpr uint32_t AML_ECC_BCH8 = 1;
// bch8 with ecc page size of 1024B.
static constexpr uint32_t AML_ECC_BCH8_1K = 2;
static constexpr uint32_t AML_ECC_BCH24_1K = 3;
static constexpr uint32_t AML_ECC_BCH30_1K = 4;
static constexpr uint32_t AML_ECC_BCH40_1K = 5;
static constexpr uint32_t AML_ECC_BCH50_1K = 6;
static constexpr uint32_t AML_ECC_BCH60_1K = 7;
// Short mode is special only for page 0 when implement booting
// from nand. it means that using a small size(384B/8=48B) of ecc page
// with a fixed ecc mode. rom code use short mode to read page0 for
// getting nand parameter such as ecc, scramber and so on.
// For gxl serial, first page adopt short mode and 60bit ecc; for axg
// serial, adopt short mode and 8bit ecc.
static constexpr uint32_t AML_ECC_BCH_SHORT = 8;

static constexpr uint32_t AML_WRITE_PAGE_TIMEOUT = 50;
static constexpr uint32_t AML_ERASE_BLOCK_TIMEOUT = 400;

#endif  // SRC_DEVICES_LIB_AMLOGIC_INCLUDE_SOC_AML_COMMON_AML_RAWNAND_H_
