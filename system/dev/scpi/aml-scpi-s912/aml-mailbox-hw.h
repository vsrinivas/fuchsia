// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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
        .payload_offset = 0x80,
    },
    // Mailbox 5
    {
        .set_offset     = 0x10,
        .stat_offset    = 0x11,
        .clr_offset     = 0x12,
        .payload_offset = 0x180,
    },
};
