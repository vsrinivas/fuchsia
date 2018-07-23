// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#define NAND_CE0 (0xe << 10)
#define NAND_CE1 (0xd << 10)

#define NAND_NCE 0x01
#define NAND_CLE 0x02
#define NAND_ALE 0x04

#define NAND_CTRL_CLE (NAND_NCE | NAND_CLE)
#define NAND_CTRL_ALE (NAND_NCE | NAND_ALE)
#define NAND_CTRL_CHANGE 0x80

#define NAND_CMD_READ0 0
#define NAND_CMD_READ1 1
#define NAND_CMD_PAGEPROG 0x10
#define NAND_CMD_READOOB 0x50
#define NAND_CMD_ERASE1 0x60
#define NAND_CMD_STATUS 0x70
#define NAND_CMD_SEQIN 0x80
#define NAND_CMD_READID 0x90
#define NAND_CMD_ERASE2 0xd0
#define NAND_CMD_RESET 0xff
#define NAND_CMD_NONE -1

/* Extended commands for large page devices */
#define NAND_CMD_READSTART 0x30

/* Status */
#define NAND_STATUS_FAIL 0x01
#define NAND_STATUS_FAIL_N1 0x02
#define NAND_STATUS_TRUE_READY 0x20
#define NAND_STATUS_READY 0x40
#define NAND_STATUS_WP 0x80

struct nand_timings {
    uint32_t tRC_min;
    uint32_t tREA_max;
    uint32_t RHOH_min;
};

struct nand_chip_table {
    uint8_t manufacturer_id;
    uint8_t device_id;
    const char* manufacturer_name;
    const char* device_name;
    struct nand_timings timings;
    uint32_t chip_delay_us;     /* delay us after enqueuing command */
    /*
     * extended_id_nand -> pagesize, erase blocksize, OOB size
     * could vary given the same device id.
     */
    bool extended_id_nand;
    uint64_t chipsize; /* MiB */
    /* Valid only if extended_id_nand is false */
    uint32_t page_size;        /* bytes */
    uint32_t oobsize;          /* bytes */
    uint32_t erase_block_size; /* bytes */
    uint32_t bus_width;        /* 8 vs 16 bit */
};

#define MAX(A, B) ((A > B) ? A : B)
#define MIN(A, B) ((A < B) ? A : B)

struct nand_chip_table* find_nand_chip_table(uint8_t manuf_id,
                                             uint8_t device_id);
void onfi_command(raw_nand_protocol_t* proto, uint32_t command,
                  int32_t column, int32_t page_addr,
                  uint32_t capacity_mb, uint32_t chip_delay_us,
                  int buswidth_16);
zx_status_t onfi_wait(raw_nand_protocol_t* proto, uint32_t timeout_ms);
