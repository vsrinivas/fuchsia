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

#define _ALL_SOURCE
#include <threads.h>

#include "core.h"
#include "debug.h"
#include "msg_buf.h"
#include "wmi-tlv.h"

// Information about our message types. This doesn't have to be in the same order as the
// ath10k_msg_type enums, but in order for the init algorithm to work properly, a type
// must be defined in the init_data array before it appears in an 'isa' field.
#define STR_NAME(name) #name
#define MSG(type, base, hdr) { type, base, hdr, STR_NAME(type) }
static const struct {
    enum ath10k_msg_type type;
    enum ath10k_msg_type isa;
    size_t hdr_size;
    const char* name;
} ath10k_msg_types_init_data[] = {
    {ATH10K_MSG_TYPE_BASE, 0, 0, "ATH10K_MSG_TYPE_BASE"},

    HTC_MSGS,

    // Note that since all of the following use the HTC interface they must follow HTC_MSGS
    WMI_MSGS,
    WMI_TLV_MSGS,
    HTT_MSGS
};
#undef MSG

// Table to keep track of the sizes and types of each message. Once initialized, this data
// is constant so we only keep a single copy. This is perhaps a terrible idea, but it does
// allow us to have a fairly compact representation of the message types in
// ath10k_msg_types_init_data (above), which is the structure most likely to require
// ongoing maintenance.
static struct ath10k_msg_type_info {
    enum ath10k_msg_type isa;
    size_t offset;
    size_t hdr_size;
    const char* name;
} ath10k_msg_types_info[ATH10K_MSG_TYPE_COUNT];
static mtx_t ath10k_msg_types_lock = MTX_INIT;
static bool ath10k_msg_types_initialized = false;

// One-time initialization of the module
zx_status_t ath10k_msg_bufs_init(struct ath10k* ar) {

    struct ath10k_msg_buf_state* state = &ar->msg_buf_state;
    state->ar = ar;

    // Clear the buffer pool
    mtx_init(&state->lock, mtx_plain);
    for (size_t ndx = 0; ndx < ATH10K_MSG_TYPE_COUNT; ndx++) {
        list_initialize(&state->buf_pool[ndx]);
    }

    // Organize our msg type information into something more usable (an array indexed by msg
    // type, with total size information).
    mtx_lock(&ath10k_msg_types_lock);
    if (!ath10k_msg_types_initialized) {
        for (size_t ndx = 0; ndx < countof(ath10k_msg_types_init_data); ndx++) {
            enum ath10k_msg_type type = ath10k_msg_types_init_data[ndx].type;
            enum ath10k_msg_type parent_type = ath10k_msg_types_init_data[ndx].isa;
            struct ath10k_msg_type_info* type_info = &ath10k_msg_types_info[type];

            type_info->isa = parent_type;
            type_info->offset = ath10k_msg_types_info[parent_type].offset
                                + ath10k_msg_types_info[parent_type].hdr_size;
            type_info->hdr_size = ath10k_msg_types_init_data[ndx].hdr_size;
            type_info->name = ath10k_msg_types_init_data[ndx].name;
        }
        ath10k_msg_types_initialized = true;
    }
    mtx_unlock(&ath10k_msg_types_lock);

    return ZX_OK;
}

zx_status_t ath10k_msg_buf_alloc(struct ath10k* ar,
                                 struct ath10k_msg_buf** msg_buf_ptr,
                                 enum ath10k_msg_type type, size_t extra_bytes) {
    struct ath10k_msg_buf_state* state = &ar->msg_buf_state;
    zx_status_t status;

    ZX_DEBUG_ASSERT(type < ATH10K_MSG_TYPE_COUNT);

    struct ath10k_msg_buf* msg_buf;

    // First, see if we have any available buffers in our pool
    mtx_lock(&state->lock);
    list_node_t* buf_list = &state->buf_pool[type];
    if (extra_bytes == 0 && !list_is_empty(buf_list)) {
        msg_buf = list_remove_head_type(buf_list, struct ath10k_msg_buf, listnode);
        mtx_unlock(&state->lock);
        ZX_DEBUG_ASSERT(msg_buf->type == type);
        ZX_DEBUG_ASSERT(msg_buf->capacity == ath10k_msg_types_info[type].offset
                                             + ath10k_msg_types_info[type].hdr_size);
    } else {
        // Allocate a new buffer
        mtx_unlock(&state->lock);
        msg_buf = calloc(1, sizeof(struct ath10k_msg_buf));
        if (!msg_buf) {
            return ZX_ERR_NO_MEMORY;
        }

        size_t buf_sz = ath10k_msg_types_info[type].offset
                        + ath10k_msg_types_info[type].hdr_size
                        + extra_bytes;
        status = io_buffer_init(&msg_buf->buf, buf_sz, IO_BUFFER_RW | IO_BUFFER_CONTIG);
        if (status != ZX_OK) {
            free(msg_buf);
            return status;
        }

        msg_buf->state = state;
        msg_buf->paddr = io_buffer_phys(&msg_buf->buf);
        msg_buf->vaddr = io_buffer_virt(&msg_buf->buf);
        memset(msg_buf->vaddr, 0, buf_sz);
        msg_buf->capacity = buf_sz;
        msg_buf->type = type;
    }
    list_initialize(&msg_buf->listnode);
    msg_buf->used = msg_buf->capacity;
    *msg_buf_ptr = msg_buf;
    return ZX_OK;
}

void* ath10k_msg_buf_get_header(struct ath10k_msg_buf* msg_buf,
                                enum ath10k_msg_type type) {
    return (void*)((uint8_t*)msg_buf->vaddr + ath10k_msg_types_info[type].offset);
}

void* ath10k_msg_buf_get_payload(struct ath10k_msg_buf* msg_buf) {
    enum ath10k_msg_type type = msg_buf->type;
    return (void*)((uint8_t*)msg_buf->vaddr
                   + ath10k_msg_types_info[type].offset
                   + ath10k_msg_types_info[type].hdr_size);
}

size_t ath10k_msg_buf_get_offset(enum ath10k_msg_type type) {
    return ath10k_msg_types_info[type].offset;
}

size_t ath10k_msg_buf_get_payload_offset(enum ath10k_msg_type type) {
    return ath10k_msg_types_info[type].offset + ath10k_msg_types_info[type].hdr_size;
}

void ath10k_msg_buf_free(struct ath10k_msg_buf* msg_buf) {
    struct ath10k_msg_buf_state* state = msg_buf->state;
    enum ath10k_msg_type type = msg_buf->type;
    ZX_DEBUG_ASSERT(type < ATH10K_MSG_TYPE_COUNT);
    msg_buf->used = 0;

    if (msg_buf->capacity == ath10k_msg_buf_get_payload_offset(type)) {
        // Retain in our buffer pool
        mtx_lock(&state->lock);
        list_clear_node(&msg_buf->listnode);
        list_add_head(&state->buf_pool[type], &msg_buf->listnode);
        mtx_unlock(&state->lock);
    } else {
        // Non-standard size, don't try to reuse
        io_buffer_release(&msg_buf->buf);
        free(msg_buf);
    }
}

void ath10k_msg_buf_dump(struct ath10k_msg_buf* msg_buf, const char* prefix) {
    uint8_t* raw_data = msg_buf->vaddr;
    ath10k_info("msg_buf (%s): paddr %#x\n",
                ath10k_msg_types_info[msg_buf->type].name,
                (unsigned int)msg_buf->paddr);
    unsigned ndx;
    for (ndx = 0; msg_buf->used - ndx >= 4; ndx += 4) {
        ath10k_info("%s0x%02x 0x%02x 0x%02x 0x%02x\n", prefix,
                    raw_data[ndx], raw_data[ndx + 1], raw_data[ndx + 2], raw_data[ndx + 3]);
    }
    if (ndx != msg_buf->used) {
        ath10k_err("%sBuffer has %d bytes extra\n", prefix, (int)(msg_buf->used - ndx));
    }
}
