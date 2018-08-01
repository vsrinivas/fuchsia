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

#include "msg_buf.h"

#include "core.h"
#include "debug.h"
#include "hif.h"
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

void ath10k_msg_bufs_init_stats(struct ath10k_msg_buf_state* state) {
    list_initialize(&state->bufs_in_use);
}

// The number of buffers to pre-allocate. This is primarily necessary because of ZX-1073:
// if we don't allocate all needed MMIO at startup, we may not be able to allocate it later
// since we need 32b addresses, and the io_buffer_t interface doesn't provide any way to
// ask for it.
#define ATH10K_INITIAL_BUF_COUNT 2560

// One-time initialization of the module
zx_status_t ath10k_msg_bufs_init(struct ath10k* ar) {

    static mtx_t init_lock = MTX_INIT;
    static bool initialized = false;

    mtx_lock(&init_lock);
    if (initialized) {
        mtx_unlock(&init_lock);
        return ZX_OK;
    }
    initialized = true;
    mtx_unlock(&init_lock);

    struct ath10k_msg_buf_state* state = &ar->msg_buf_state;
    state->ar = ar;

    // Clear the buffer pool
    mtx_init(&state->lock, mtx_plain);
    list_initialize(&state->buf_pool);

#if DEBUG_MSG_BUF
    ath10k_msg_bufs_init_stats(state);
#endif

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

    struct ath10k_msg_buf* msg_buf;
    for (unsigned i = 0; i < ATH10K_INITIAL_BUF_COUNT; i++) {
        ath10k_msg_buf_alloc_internal(ar, &msg_buf, ATH10K_MSG_TYPE_BASE, 1, true,
                                      __FILE__, __LINE__);
        ath10k_msg_buf_free(msg_buf);
    }

    return ZX_OK;
}

zx_status_t ath10k_msg_buf_alloc_internal(struct ath10k* ar,
                                          struct ath10k_msg_buf** msg_buf_ptr,
                                          enum ath10k_msg_type type,
                                          size_t extra_bytes,
                                          bool force_new,
                                          const char* filename,
                                          size_t line_num) {
    struct ath10k_msg_buf_state* state = &ar->msg_buf_state;
    zx_status_t status;

    ZX_DEBUG_ASSERT(type < ATH10K_MSG_TYPE_COUNT);

    struct ath10k_msg_buf* msg_buf;
    size_t requested_sz = ath10k_msg_types_info[type].offset
                          + ath10k_msg_types_info[type].hdr_size
                          + extra_bytes;
    ZX_DEBUG_ASSERT(requested_sz > 0);
    ZX_DEBUG_ASSERT(requested_sz <= PAGE_SIZE);

    // First, see if we have any available buffers in our pool
    mtx_lock(&state->lock);
    if (!list_is_empty(&state->buf_pool) && !force_new) {
        msg_buf = list_remove_head_type(&state->buf_pool, struct ath10k_msg_buf, listnode);
        ZX_DEBUG_ASSERT(msg_buf->capacity == PAGE_SIZE);
        ZX_DEBUG_ASSERT(msg_buf->state == state);
        mtx_unlock(&state->lock);
        io_buffer_cache_flush_invalidate(&msg_buf->buf, 0, PAGE_SIZE);
    } else {
        // Allocate a new buffer
        mtx_unlock(&state->lock);
        msg_buf = calloc(1, sizeof(struct ath10k_msg_buf));
        if (!msg_buf) {
            return ZX_ERR_NO_MEMORY;
        }

        zx_handle_t bti_handle;
        status = ath10k_hif_get_bti_handle(ar, &bti_handle);
        if (status != ZX_OK) {
            goto err_free_buf;
        }
        status = io_buffer_init(&msg_buf->buf, bti_handle, PAGE_SIZE,
                                IO_BUFFER_RW | IO_BUFFER_CONTIG);
        if (status != ZX_OK) {
            goto err_free_buf;
        }

        msg_buf->paddr = io_buffer_phys(&msg_buf->buf);
        if (msg_buf->paddr + PAGE_SIZE > 0x100000000) {
            status = ZX_ERR_NO_MEMORY;
            ath10k_warn("attempt to allocate buffer, unable to get mmio with "
                        "32 bit phys addr (see ZX-1073)\n");
            goto err_free_iobuf;
        }
        msg_buf->vaddr = io_buffer_virt(&msg_buf->buf);
        msg_buf->capacity = PAGE_SIZE;
        msg_buf->state = state;
    }

    memset(msg_buf->vaddr, 0, requested_sz);
    msg_buf->type = type;
    msg_buf->used = requested_sz;
#if DEBUG_MSG_BUF
    msg_buf->alloc_file_name = filename;
    msg_buf->alloc_line_num = line_num;
    mtx_lock(&state->lock);
    list_add_tail(&state->bufs_in_use, &msg_buf->debug_listnode);
    mtx_unlock(&state->lock);
#endif
    *msg_buf_ptr = msg_buf;
    return ZX_OK;

err_free_iobuf:
    io_buffer_release(&msg_buf->buf);

err_free_buf:
    free(msg_buf);
    return status;
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

size_t ath10k_msg_buf_get_payload_len(struct ath10k_msg_buf* msg_buf,
                                      enum ath10k_msg_type msg_type) {
    return msg_buf->used - ath10k_msg_buf_get_payload_offset(msg_type);
}

size_t ath10k_msg_buf_get_offset(enum ath10k_msg_type type) {
    return ath10k_msg_types_info[type].offset;
}

size_t ath10k_msg_buf_get_payload_offset(enum ath10k_msg_type type) {
    return ath10k_msg_types_info[type].offset + ath10k_msg_types_info[type].hdr_size;
}

void ath10k_msg_buf_free(struct ath10k_msg_buf* msg_buf) {
    struct ath10k_msg_buf_state* state = msg_buf->state;

    ZX_DEBUG_ASSERT(msg_buf->capacity == PAGE_SIZE);
#if DEBUG_MSG_BUF
    mtx_lock(&state->lock);
    list_delete(&msg_buf->debug_listnode);
    mtx_unlock(&state->lock);
#endif
    // Save in pool for reuse
    mtx_lock(&state->lock);
    ZX_DEBUG_ASSERT_MSG(msg_buf->used != 0, "attempt to free already freed buffer");
    msg_buf->used = 0;
    list_add_head(&state->buf_pool, &msg_buf->listnode);
    mtx_unlock(&state->lock);
}

#if DEBUG_MSG_BUF

#define MAX_BUFFER_LOCS 16

static void dump_buffer_locs(list_node_t* buf_list) {
    struct {
        const char* filename;
        size_t line_number;
        size_t count;
    } buffer_origins[MAX_BUFFER_LOCS];

    // Initialize
    for (size_t ndx = 0; ndx < MAX_BUFFER_LOCS; ndx++) {
        buffer_origins[ndx].count = 0;
    }

    // Count
    struct ath10k_msg_buf* next_buf;
    list_for_every_entry(buf_list, next_buf, struct ath10k_msg_buf, debug_listnode) {
        size_t ndx;
        for (ndx = 0; ndx < MAX_BUFFER_LOCS; ndx++) {
            if (buffer_origins[ndx].count == 0) {
                buffer_origins[ndx].filename = next_buf->alloc_file_name;
                buffer_origins[ndx].line_number = next_buf->alloc_line_num;
                buffer_origins[ndx].count = 1;
                break;
            } else if ((buffer_origins[ndx].line_number == next_buf->alloc_line_num)
                       && !strcmp(buffer_origins[ndx].filename, next_buf->alloc_file_name)) {
                buffer_origins[ndx].count++;
                break;
            }
        }
        ZX_DEBUG_ASSERT(ndx < MAX_BUFFER_LOCS);
    }

    // Report
    printf("  Buffer origins:\n");
    for (size_t ndx = 0; ndx < MAX_BUFFER_LOCS && buffer_origins[ndx].count != 0; ndx++) {
        printf("    %s:%zd... %zd\n",
               buffer_origins[ndx].filename,
               buffer_origins[ndx].line_number,
               buffer_origins[ndx].count);
    }
}

void ath10k_msg_buf_dump_stats(struct ath10k* ar) {
    struct ath10k_msg_buf_state* state = &ar->msg_buf_state;
    mtx_lock(&state->lock);
    printf("msg_buf stats:\n");
    printf("  Buffers in use: %d\n", (int)list_length(&state->bufs_in_use));
    printf("  Buffers available for reuse: %zd\n", list_length(&state->buf_pool));
    dump_buffer_locs(&state->bufs_in_use);
    mtx_unlock(&state->lock);
}
#endif // DEBUG_MSG_BUF

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
