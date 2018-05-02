// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hw/reg.h>

#define READ32_MAILBOX_PL_REG(offset)         readl(io_buffer_virt(&fanctl->mmio_mailbox_payload) \
                                                + offset*4)
#define WRITE32_MAILBOX_PL_REG(offset, value) writel(value, \
                                                     io_buffer_virt(&fanctl->mmio_mailbox_payload) \
                                                     + offset*4)
#define READ32_MAILBOX_REG(offset)            readl(io_buffer_virt(&fanctl->mmio_mailbox) \
                                                    + offset*4)
#define WRITE32_MAILBOX_REG(offset, value)    writel(value, io_buffer_virt(&fanctl->mmio_mailbox) \
                                                     + offset*4)

#define SCP_CMD_SENSOR_CAPABILITIES     0x041A
#define SCP_CMD_SENSOR_INFO             0x041B
#define SCP_CMD_SENSOR_VALUE            0x041C

#define GET_NUM_WORDS(x) ((x)/4 + (((x)%4)? 1:0))

typedef struct {
    uint32_t set_offset;
    uint32_t stat_offset;
    uint32_t clr_offset;
    uint32_t payload_offset;
} aml_mailbox_block_t;

typedef struct {
    uint32_t cmd;
    uint32_t tx_size;
    void *tx_buf;
    void *rx_buf;
    uint32_t rx_size;
    uint32_t rx_mailbox;
    uint32_t tx_mailbox;
} aml_mhu_data_buf_t;

static aml_mailbox_block_t vim2_mailbox_block[] = {
    // Mailbox 0
    {
        .set_offset     = 0x1,
        .stat_offset    = 0x2,
        .clr_offset     = 0x3,
        .payload_offset = 0x200,
    },
    // Mailbox 1
    {
        .set_offset     = 0x4,
        .stat_offset    = 0x5,
        .clr_offset     = 0x6,
        .payload_offset = 0x0,
    },
    // Mailbox 2
    {
        .set_offset     = 0x7,
        .stat_offset    = 0x8,
        .clr_offset     = 0x9,
        .payload_offset = 0x100,
    },
    // Mailbox 3
    {
        .set_offset     = 0xA,
        .stat_offset    = 0xB,
        .clr_offset     = 0xC,
        .payload_offset = 0x280,
    },
    // Mailbox 4
    {
        .set_offset     = 0xD,
        .stat_offset    = 0xE,
        .clr_offset     = 0xF,
        .payload_offset = 0x128,
    },
    // Mailbox 5
    {
        .set_offset     = 0x10,
        .stat_offset    = 0x11,
        .clr_offset     = 0x12,
        .payload_offset = 0x180,
    },
};
