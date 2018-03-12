/*
 * Copyright 2018 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include <stdlib.h>

#include <zircon/listnode.h>
#include <zircon/status.h>

#include "htc.h"
#include "htt.h"
#include "wmi.h"
#include "wmi-tlv.h"

// Each of the modules that generate or parse messages should instantiate its
// top-level macro (e.g., WMI_MSGS) with a comma-delimited list of messages,
// defined as MSG(name, base-name, type). The top-level macros will be used
// whenever a complete list of messages will be needed, with MSG defined to
// match the context where it is being used.
#define MSG(type, base, hdr) type
enum ath10k_msg_type {
    ATH10K_MSG_TYPE_BASE,

    // Instantiated from htc.h
    HTC_MSGS,

    // Instantiated from wmi.h
    WMI_MSGS,

    // Instantiated from wmi-tlv.h
    WMI_TLV_MSGS,

    // Instantiated from htt.h
    HTT_MSGS,

    // Must be last
    ATH10K_MSG_TYPE_COUNT
};
#undef MSG

struct ath10k_msg_buf {
    struct ath10k_msg_buf_state* state;
    enum ath10k_msg_type type;
    list_node_t listnode;
    io_buffer_t buf;
    void* vaddr;
    zx_paddr_t paddr;
    size_t capacity;
    size_t used;
};

struct ath10k_msg_buf_state {
    struct ath10k* ar;
    mtx_t lock;

    // Lists of previously-allocated buffers of each message type
    list_node_t buf_pool[ATH10K_MSG_TYPE_COUNT];
};

// Initialize the module
zx_status_t ath10k_msg_bufs_init(struct ath10k* ar);

// Allocate a new buffer of the specified type, plus any extra space requested
zx_status_t ath10k_msg_buf_alloc(struct ath10k* ar,
                                 struct ath10k_msg_buf** msg_buf_ptr,
                                 enum ath10k_msg_type type,
                                 size_t extra_bytes);

void* ath10k_msg_buf_get_header(struct ath10k_msg_buf* msg_buf,
                                enum ath10k_msg_type msg_type);

void* ath10k_msg_buf_get_payload(struct ath10k_msg_buf* msg_buf,
                                 enum ath10k_msg_type msg_type);

size_t ath10k_msg_buf_get_offset(enum ath10k_msg_type msg_type);

size_t ath10k_msg_buf_get_payload_offset(enum ath10k_msg_type msg_type);

void ath10k_msg_buf_free(struct ath10k_msg_buf* msg_buf);

