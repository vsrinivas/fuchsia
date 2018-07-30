// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hw/reg.h>
#include <lib/sync/completion.h>
#include <ddk/protocol/platform-bus.h>
#include <ddk/protocol/platform-defs.h>
#include <ddk/protocol/platform-device.h>
#include <ddk/protocol/mailbox.h>
#include <ddk/debug.h>

#define MAILBOX_ERROR(fmt, ...) zxlogf(ERROR, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)
#define MAILBOX_INFO(fmt, ...) zxlogf(INFO, "[%s %d]" fmt, __func__, __LINE__, ##__VA_ARGS__)

#define NUM_MAILBOXES    6
#define GET_NUM_WORDS(x) ((x)/4 + (((x)%4)? 1:0))

typedef struct {
    uint32_t set_offset;
    uint32_t stat_offset;
    uint32_t clr_offset;
    uint32_t payload_offset;
} aml_mailbox_block_t;

typedef struct {
    zx_device_t*                        zxdev;
    platform_device_protocol_t          pdev;
    mailbox_protocol_t                  mailbox;

    io_buffer_t                         mmio_mailbox;
    io_buffer_t                         mmio_mailbox_payload;

    zx_handle_t                         inth[NUM_MAILBOXES];

    mtx_t                               mailbox_chan_lock[NUM_MAILBOXES];
} aml_mailbox_t;

// MMIO Indexes
enum {
    MMIO_MAILBOX,
    MMIO_MAILBOX_PAYLOAD,
};

// IRQ Indexes
enum {
    MAILBOX_IRQ_RECEIV0,
    MAILBOX_IRQ_RECEIV1,
    MAILBOX_IRQ_RECEIV2,
    MAILBOX_IRQ_SEND3,
    MAILBOX_IRQ_SEND4,
    MAILBOX_IRQ_SEND5,
};

// Mailboxes
enum {
    SCP_SECURE_MAILBOX,
    SCP_NS_LOW_PRIORITY_MAILBOX,
    SCP_NS_HIGH_PRIORITY_MAILBOX,
    AP_SECURE_MAILBOX,
    AP_NS_LOW_PRIORITY_MAILBOX,
    AP_NS_HIGH_PRIORITY_MAILBOX,
    INVALID_MAILBOX,
};