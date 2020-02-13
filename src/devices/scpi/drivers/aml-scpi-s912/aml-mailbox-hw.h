// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SCPI_DRIVERS_AML_SCPI_S912_AML_MAILBOX_HW_H_
#define SRC_DEVICES_SCPI_DRIVERS_AML_SCPI_S912_AML_MAILBOX_HW_H_

typedef struct {
  uint32_t set_offset;
  uint32_t stat_offset;
  uint32_t clr_offset;
  uint32_t payload_offset;
} aml_mailbox_block_t;

static aml_mailbox_block_t vim2_mailbox_block[] = {
    // Mailbox 0
    {
        .set_offset = 0x1 << 2,
        .stat_offset = 0x2 << 2,
        .clr_offset = 0x3 << 2,
        .payload_offset = 0x200 << 2,
    },
    // Mailbox 1
    {
        .set_offset = 0x4 << 2,
        .stat_offset = 0x5 << 2,
        .clr_offset = 0x6 << 2,
        .payload_offset = 0x0 << 2,
    },
    // Mailbox 2
    {
        .set_offset = 0x7 << 2,
        .stat_offset = 0x8 << 2,
        .clr_offset = 0x9 << 2,
        .payload_offset = 0x100 << 2,
    },
    // Mailbox 3
    {
        .set_offset = 0xA << 2,
        .stat_offset = 0xB << 2,
        .clr_offset = 0xC << 2,
        .payload_offset = 0x280 << 2,
    },
    // Mailbox 4
    {
        .set_offset = 0xD << 2,
        .stat_offset = 0xE << 2,
        .clr_offset = 0xF << 2,
        .payload_offset = 0x80 << 2,
    },
    // Mailbox 5
    {
        .set_offset = 0x10 << 2,
        .stat_offset = 0x11 << 2,
        .clr_offset = 0x12 << 2,
        .payload_offset = 0x180 << 2,
    },
};

#endif  // SRC_DEVICES_SCPI_DRIVERS_AML_SCPI_S912_AML_MAILBOX_HW_H_
