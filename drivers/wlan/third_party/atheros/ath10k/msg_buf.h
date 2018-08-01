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

#define _ALL_SOURCE  // To get MTX_INIT and thrd_create_with_name from threads.h. This must be
                     // defined before other project header files are included. Otherwise any
                     // other header file may include threads.h without defining this.
#include <threads.h>

#include <zircon/listnode.h>
#include <zircon/status.h>

#include "htc.h"
#include "htt.h"
#include "wmi.h"
#include "wmi-tlv.h"

#define DEBUG_MSG_BUF 0

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

enum ath10k_tx_flags {
    ATH10K_TX_BUF_PROTECTED = (1 << 0),
    ATH10K_TX_BUF_QOS =       (1 << 1),
};

struct ath10k_msg_buf {
    struct ath10k_msg_buf_state* state;
    enum ath10k_msg_type type;
    list_node_t listnode;
    io_buffer_t buf;
    void* vaddr;
    zx_paddr_t paddr;
    size_t capacity;
    size_t used;

    // Tx/Rx meta-data. They differ because Tx arrives from the target wrapped in an HTT
    // packet, and Rx is passed to us from the wlan driver as a raw packet.
    union {
        struct {
            // These tell us how to find the packet within the HTT message
            size_t frame_offset;
            size_t frame_size;
        } rx;

        struct {
            uint32_t flags;  // ATH10K_TX_BUF_*
        } tx;
    };

#if DEBUG_MSG_BUF
    // Fields used for analysis/debugging
    const char* alloc_file_name;
    size_t alloc_line_num;
    list_node_t debug_listnode;
#endif
};

struct ath10k_msg_buf_state {
    struct ath10k* ar;
    mtx_t lock;

    // Lists of previously-allocated buffers
    list_node_t buf_pool;

    // Used for analysis/debugging
    list_node_t bufs_in_use;
};

// Initialize the module
zx_status_t ath10k_msg_bufs_init(struct ath10k* ar);

// Allocate a new buffer of the specified type, plus any extra space requested
zx_status_t ath10k_msg_buf_alloc_internal(struct ath10k* ar,
                                          struct ath10k_msg_buf** msg_buf_ptr,
                                          enum ath10k_msg_type type,
                                          size_t extra_bytes,
                                          bool force_new,
                                          const char* filename,
                                          size_t line_num);

#define ath10k_msg_buf_alloc(ar, ptr, type, bytes) \
        ath10k_msg_buf_alloc_internal(ar, ptr, type, bytes, false, __FILE__, __LINE__)

void* ath10k_msg_buf_get_header(struct ath10k_msg_buf* msg_buf,
                                enum ath10k_msg_type msg_type);

void* ath10k_msg_buf_get_payload(struct ath10k_msg_buf* msg_buf);

size_t ath10k_msg_buf_get_payload_len(struct ath10k_msg_buf* msg_buf,
                                      enum ath10k_msg_type msg_type);

size_t ath10k_msg_buf_get_offset(enum ath10k_msg_type msg_type);

size_t ath10k_msg_buf_get_payload_offset(enum ath10k_msg_type msg_type);

void ath10k_msg_buf_free(struct ath10k_msg_buf* msg_buf);

void ath10k_msg_buf_dump_stats(struct ath10k* ar);

void ath10k_msg_buf_dump(struct ath10k_msg_buf* msg_buf, const char* prefix);
