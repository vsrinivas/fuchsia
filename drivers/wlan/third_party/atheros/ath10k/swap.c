/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
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

/* This file has implementation for code swap logic. With code swap feature,
 * target can run the fw binary with even smaller IRAM size by using host
 * memory to store some of the code segments.
 */

#include <string.h>

#include <ddk/io-buffer.h>
#include <zircon/status.h>

#include "bmi.h"
#include "core.h"
#include "debug.h"
#include "hif.h"

static zx_status_t ath10k_swap_code_seg_fill(struct ath10k* ar,
        struct ath10k_swap_code_seg_info* seg_info,
        const void* data, size_t data_len) {
    uint8_t* virt_addr = seg_info->virt_address[0];
    uint8_t swap_magic[ATH10K_SWAP_CODE_SEG_MAGIC_BYTES_SZ] = {};
    const uint8_t* fw_data = data;
    union ath10k_swap_code_seg_item* swap_item;
    uint32_t length = 0;
    uint32_t payload_len;
    uint32_t total_payload_len = 0;
    uint32_t size_left = data_len;

    /* Parse swap bin and copy the content to host allocated memory.
     * The format is Address, length and value. The last 4-bytes is
     * target write address. Currently address field is not used.
     */
    seg_info->target_addr = -1;
    while (size_left >= sizeof(*swap_item)) {
        swap_item = (union ath10k_swap_code_seg_item*)fw_data;
        payload_len = swap_item->tlv.length;
        if ((payload_len > size_left) ||
                (payload_len == 0 &&
                 size_left != sizeof(struct ath10k_swap_code_seg_tail))) {
            ath10k_err("refusing to parse invalid tlv length %d\n",
                       payload_len);
            return ZX_ERR_INVALID_ARGS;
        }

        if (payload_len == 0) {
            if (memcmp(swap_item->tail.magic_signature, swap_magic,
                       ATH10K_SWAP_CODE_SEG_MAGIC_BYTES_SZ)) {
                ath10k_err("refusing an invalid swap file\n");
                return ZX_ERR_INVALID_ARGS;
            }
            seg_info->target_addr = swap_item->tail.bmi_write_addr;
            break;
        }

        memcpy(virt_addr, swap_item->tlv.data, payload_len);
        virt_addr += payload_len;
        length = payload_len +  sizeof(struct ath10k_swap_code_seg_tlv);
        size_left -= length;
        fw_data += length;
        total_payload_len += payload_len;
    }

    if (seg_info->target_addr == (uint32_t)-1) {
        ath10k_err("failed to parse invalid swap file\n");
        return ZX_ERR_INVALID_ARGS;
    }
    seg_info->seg_hw_info.swap_size = total_payload_len;

    return ZX_OK;
}

static void
ath10k_swap_code_seg_free(struct ath10k* ar,
                          struct ath10k_swap_code_seg_info* seg_info) {
    uint32_t seg_size;

    if (!seg_info) {
        return;
    }

    if (!seg_info->virt_address[0]) {
        return;
    }

    seg_size = seg_info->seg_hw_info.size;
    io_buffer_release(&seg_info->handles[0]);
}

static struct ath10k_swap_code_seg_info*
ath10k_swap_code_seg_alloc(struct ath10k* ar, size_t swap_bin_len) {
    struct ath10k_swap_code_seg_info* seg_info;
    void* virt_addr;
    zx_paddr_t paddr;
    zx_status_t ret;

    swap_bin_len = ROUNDUP(swap_bin_len, 2);
    if (swap_bin_len > ATH10K_SWAP_CODE_SEG_BIN_LEN_MAX) {
        ath10k_err("refusing code swap bin because it is too big %zu > %d\n",
                   swap_bin_len, ATH10K_SWAP_CODE_SEG_BIN_LEN_MAX);
        return NULL;
    }

    seg_info = calloc(1, sizeof(*seg_info));
    if (!seg_info) {
        return NULL;
    }

    zx_handle_t bti_handle;
    ret = ath10k_hif_get_bti_handle(ar, &bti_handle);
    if (ret != ZX_OK) {
        ath10k_err("unable to retrieve BTI handle\n");
        goto err_free_seg_info;
    }
    ret = io_buffer_init(&seg_info->handles[0], bti_handle, swap_bin_len,
                         IO_BUFFER_RW | IO_BUFFER_CONTIG);
    if (ret != ZX_OK) {
        ath10k_err("failed to allocate dma memory\n");
        goto err_free_seg_info;
    }

    paddr = io_buffer_phys(&seg_info->handles[0]);
    virt_addr = io_buffer_virt(&seg_info->handles[0]);
    if (paddr + swap_bin_len > 0x100000000ULL) {
        ath10k_err("io buffer allocated with address above 32b range (see ZX-1073)\n");
        goto err_free_io_buf;
    }

    seg_info->seg_hw_info.bus_addr[0] = paddr;
    seg_info->seg_hw_info.size = swap_bin_len;
    seg_info->seg_hw_info.swap_size = swap_bin_len;
    seg_info->seg_hw_info.num_segs = ATH10K_SWAP_CODE_SEG_NUM_SUPPORTED;
    seg_info->seg_hw_info.size_log2 = LOG2(swap_bin_len);
    seg_info->virt_address[0] = virt_addr;
    seg_info->paddr[0] = paddr;

    return seg_info;

err_free_io_buf:
    io_buffer_release(&seg_info->handles[0]);
err_free_seg_info:
    free(seg_info);
    return NULL;
}

zx_status_t ath10k_swap_code_seg_configure(struct ath10k* ar,
        const struct ath10k_fw_file* fw_file) {
    zx_status_t ret;
    struct ath10k_swap_code_seg_info* seg_info = NULL;

    if (!fw_file->firmware_swap_code_seg_info) {
        return ZX_OK;
    }

    ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot found firmware code swap binary\n");

    seg_info = fw_file->firmware_swap_code_seg_info;

    ret = ath10k_bmi_write_memory(ar, seg_info->target_addr,
                                  &seg_info->seg_hw_info,
                                  sizeof(seg_info->seg_hw_info));
    if (ret != ZX_OK) {
        ath10k_err("failed to write Code swap segment information (%s)\n",
                   zx_status_get_string(ret));
        return ret;
    }

    return ZX_OK;
}

void ath10k_swap_code_seg_release(struct ath10k* ar,
                                  struct ath10k_fw_file* fw_file) {
    ath10k_swap_code_seg_free(ar, fw_file->firmware_swap_code_seg_info);

    /* FIXME: these two assignments look to bein wrong place! Shouldn't
     * they be in ath10k_core_free_firmware_files() like the rest?
     */
    fw_file->codeswap_data = NULL;
    fw_file->codeswap_len = 0;

    fw_file->firmware_swap_code_seg_info = NULL;
}

zx_status_t ath10k_swap_code_seg_init(struct ath10k* ar, struct ath10k_fw_file* fw_file) {
    zx_status_t ret;
    struct ath10k_swap_code_seg_info* seg_info;
    const void* codeswap_data;
    size_t codeswap_len;

    codeswap_data = fw_file->codeswap_data;
    codeswap_len = fw_file->codeswap_len;

    if (!codeswap_len || !codeswap_data) {
        return ZX_OK;
    }

    seg_info = ath10k_swap_code_seg_alloc(ar, codeswap_len);
    if (!seg_info) {
        ath10k_err("failed to allocate fw code swap segment\n");
        return ZX_ERR_NO_MEMORY;
    }

    ret = ath10k_swap_code_seg_fill(ar, seg_info,
                                    codeswap_data, codeswap_len);

    if (ret != ZX_OK) {
        ath10k_warn("failed to initialize fw code swap segment: %s\n",
                    zx_status_get_string(ret));
        ath10k_swap_code_seg_free(ar, seg_info);
        return ret;
    }

    fw_file->firmware_swap_code_seg_info = seg_info;

    return ZX_OK;
}
