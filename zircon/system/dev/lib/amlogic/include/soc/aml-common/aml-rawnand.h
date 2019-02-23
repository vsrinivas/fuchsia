// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#define AML_NAME "aml-nand"

// TODO : The #defines here should be converted to constexprs. And the
// macros should be converted to constexpr function.

#define P_NAND_CMD      (0x00)
#define P_NAND_CFG      (0x04)
#define P_NAND_DADR     (0x08)
#define P_NAND_IADR     (0x0c)
#define P_NAND_BUF      (0x10)
#define P_NAND_INFO     (0x14)
#define P_NAND_DC       (0x18)
#define P_NAND_ADR      (0x1c)
#define P_NAND_DL       (0x20)
#define P_NAND_DH       (0x24)
#define P_NAND_CADR     (0x28)
#define P_NAND_SADR     (0x2c)
#define P_NAND_PINS     (0x30)
#define P_NAND_VER      (0x38)

#define AML_CMD_DRD     (0x8<<14)
#define AML_CMD_IDLE    (0xc<<14)
#define AML_CMD_DWR     (0x4<<14)
#define AML_CMD_CLE     (0x5<<14)
#define AML_CMD_ALE     (0x6<<14)
#define AML_CMD_ADL     ((0<<16) | (3<<20))
#define AML_CMD_ADH     ((1<<16) | (3<<20))
#define AML_CMD_AIL     ((2<<16) | (3<<20))
#define AML_CMD_AIH     ((3<<16) | (3<<20))
#define AML_CMD_SEED    ((8<<16) | (3<<20))
#define AML_CMD_M2N     ((0<<17) | (2<<20))
#define AML_CMD_N2M     ((1<<17) | (2<<20))
#define AML_CMD_RB      (1<<20)
#define AML_CMD_IO6     ((0xb<<10)|(1<<18))

#define NAND_TWB_TIME_CYCLE     10

#define CMDRWGEN(cmd_dir, ran, bch, short, pagesize, pages) \
    ((cmd_dir) | (ran) << 19 | (bch) << 14 | \
     (short) << 13 | ((pagesize)&0x7f) << 6 | ((pages)&0x3f))

#define GENCMDDADDRL(adl, addr) \
    static_cast<uint32_t>((adl) | ((addr) & 0xffff))
#define GENCMDDADDRH(adh, addr) \
    static_cast<uint32_t>((adh) | (((addr) >> 16) & 0xffff))

#define GENCMDIADDRL(ail, addr) \
    static_cast<uint32_t>((ail) | ((addr) & 0xffff))
#define GENCMDIADDRH(aih, addr) \
    static_cast<uint32_t>((aih) | (((addr) >> 16) & 0xffff))

#define RB_STA(x) (1<<(26+x))

#define AML_ECC_UNCORRECTABLE_CNT       0x3f

#define ECC_CHECK_RETURN_FF (-1)

#define DMA_BUSY_TIMEOUT 0x100000

#define CMD_FINISH_TIMEOUT_MS   1000

#define MAX_CE_NUM      2

#define RAN_ENABLE      1

#define CLK_ALWAYS_ON   (0x01 << 28)
#define AML_CLK_CYCLE   6

/* nand flash controller delay 3 ns */
#define AML_DEFAULT_DELAY 3000

#define MAX_ECC_INDEX   10

enum {
    AML_ECC_NONE    = 0,
    /* bch8 with ecc page size of 512B */
    AML_ECC_BCH8,
    /* bch8 with ecc page size of 1024B */
    AML_ECC_BCH8_1K,
    AML_ECC_BCH24_1K,
    AML_ECC_BCH30_1K,
    AML_ECC_BCH40_1K,
    AML_ECC_BCH50_1K,
    AML_ECC_BCH60_1K,

    /*
     * Short mode is special only for page 0 when implement booting
     * from nand. it means that using a small size(384B/8=48B) of ecc page
     * with a fixed ecc mode. rom code use short mode to read page0 for
     * getting nand parameter such as ecc, scramber and so on.
     * For gxl serial, first page adopt short mode and 60bit ecc; for axg
     * serial, adopt short mode and 8bit ecc.
     */
    AML_ECC_BCH_SHORT,
};

#define AML_WRITE_PAGE_TIMEOUT          2
#define AML_ERASE_BLOCK_TIMEOUT         400

